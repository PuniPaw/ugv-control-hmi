#include "ws_server.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <deque>
#include <utility>

namespace ugvc::app {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {
constexpr auto kHandshakeTimeout = std::chrono::seconds(5);
constexpr std::size_t kMaxIncomingMessage = 64 * 1024;  // IF-03 7.2 명령은 수백 바이트
}  // namespace

// 연결 하나. 비동기 작업마다 shared_from_this()를 붙잡아, 마지막 작업이 끝날 때 사라진다.
class WsServer::Session : public std::enable_shared_from_this<Session> {
 public:
  Session(WsServer& server, tcp::socket socket) : server_(server), ws_(std::move(socket)) {}

  void start() {
    beast::get_lowest_layer(ws_).expires_after(kHandshakeTimeout);
    http::async_read(ws_.next_layer(), buffer_, request_,
                     [self = shared_from_this()](beast::error_code ec, std::size_t) { self->on_request(ec); });
  }

  // 대기열에 넣기만 한다. 상한을 넘으면 버리고 센다 (호출한 쪽을 막지 않는다).
  void send(const Text& text) {
    if (!open_) return;
    if (queue_.size() >= server_.max_queued_) {
      ++server_.dropped_;
      return;
    }
    queue_.push_back(text);
    if (queue_.size() == 1) write_next();
  }

 private:
  void on_request(beast::error_code ec) {
    if (ec) return;  // 핸드셰이크 전에 끊김·시간 초과: 기록할 것이 없다
    if (!websocket::is_upgrade(request_) || request_.target() != server_.path_) {
      reject();
      return;
    }
    beast::get_lowest_layer(ws_).expires_never();  // 이후 시간 제한은 WebSocket ping/pong 설정이 맡는다
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.read_message_max(kMaxIncomingMessage);
    ws_.async_accept(request_, [self = shared_from_this()](beast::error_code accept_ec) {
      self->on_accept(accept_ec);
    });
  }

  void reject() {
    server_.cb_.log("ws reject " + std::string(request_.target()));
    auto res = std::make_shared<http::response<http::string_body>>(http::status::not_found, request_.version());
    res->set(http::field::content_type, "text/plain");
    res->body() = "IF-03 WebSocket is at " + server_.path_ + "\n";
    res->prepare_payload();
    res->keep_alive(false);
    http::async_write(ws_.next_layer(), *res, [self = shared_from_this(), res](beast::error_code, std::size_t) {
      beast::error_code ignored;
      beast::get_lowest_layer(self->ws_).socket().shutdown(tcp::socket::shutdown_send, ignored);
    });
  }

  void on_accept(beast::error_code ec) {
    if (ec) return;
    ws_.text(true);
    open_ = true;
    server_.sessions_.insert(shared_from_this());
    server_.cb_.log("ws open (" + std::to_string(server_.sessions_.size()) + " clients)");
    for (const auto& text : server_.cb_.on_open()) send(text);
    read_next();
  }

  void read_next() {
    buffer_.clear();
    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
      if (ec) {
        self->close();
        return;
      }
      self->server_.cb_.on_message(beast::buffers_to_string(self->buffer_.data()));
      self->read_next();
    });
  }

  void write_next() {
    ws_.async_write(asio::buffer(*queue_.front()), [self = shared_from_this()](beast::error_code ec, std::size_t) {
      if (ec) {
        self->close();
        return;
      }
      self->queue_.pop_front();
      if (self->open_ && !self->queue_.empty()) self->write_next();
    });
  }

  // 읽기·쓰기 중 먼저 실패한 쪽이 부른다. 두 번 불려도 한 번만 기록한다.
  // 대기열은 비우지 않는다: 진행 중인 async_write가 맨 앞 문자열을 아직 쓰고 있을 수 있다.
  void close() {
    open_ = false;
    if (server_.sessions_.erase(shared_from_this()) == 0) return;
    server_.cb_.log("ws close (" + std::to_string(server_.sessions_.size()) + " clients)");
  }

  WsServer& server_;
  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> request_;
  std::deque<Text> queue_;  // 맨 앞이 지금 쓰는 중인 메시지
  bool open_ = false;       // 핸드셰이크 후 true, 읽기·쓰기 실패 후 false
};

WsServer::WsServer(asio::io_context& io, const tcp::endpoint& endpoint, std::string path,
                   std::size_t max_queued_per_session, Callbacks callbacks)
    : acceptor_(io, endpoint), path_(std::move(path)), max_queued_(max_queued_per_session), cb_(std::move(callbacks)) {
  accept();
}

WsServer::~WsServer() {
  beast::error_code ignored;
  acceptor_.close(ignored);
}

void WsServer::broadcast(const Text& text) {
  for (const auto& s : sessions_) s->send(text);
}

std::uint16_t WsServer::port() const { return acceptor_.local_endpoint().port(); }

void WsServer::accept() {
  acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
    if (ec == asio::error::operation_aborted) return;  // 종료 중
    if (!ec) std::make_shared<Session>(*this, std::move(socket))->start();
    accept();
  });
}

}  // namespace ugvc::app
