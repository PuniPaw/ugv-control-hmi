#include "ws_server.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <deque>
#include <string_view>
#include <vector>
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
constexpr auto kAcceptRetryDelay = std::chrono::milliseconds(100);

// 요청 대상에서 경로만: "/ws?client=hmi" → "/ws"
std::string_view path_of(std::string_view target) { return target.substr(0, target.find('?')); }
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

  // 대기열에 넣기만 한다 (호출한 쪽을 막지 않는다). 상한을 넘으면 끊는다: 메모리 상한용이다.
  // 뒤처진 HMI는 보통 이보다 먼저 ping 응답 제한(max_lag)으로 끊긴다. 대기열이 넘치는 것은
  // max_lag보다 짧은 시간에 메시지가 몰린 경우다. 진행 중인 읽기·쓰기는 소켓을 닫으면 오류로 끝나고,
  // 그때 세션이 사라진다.
  void send(const Text& text) {
    if (!open_) return;
    if (queue_.size() >= server_.limits_.max_queued_per_session) {
      ++server_.slow_closed_;
      close();
      beast::error_code ignored;
      beast::get_lowest_layer(ws_).socket().close(ignored);
      return;
    }
    queue_.push_back(text);
    if (queue_.size() == 1) write_next();
  }

 private:
  void on_request(beast::error_code ec) {
    if (ec) return;  // 핸드셰이크 전에 끊김·시간 초과: 기록할 것이 없다
    if (!websocket::is_upgrade(request_) || path_of(request_.target()) != server_.path_) {
      reject(http::status::not_found, "IF-03 WebSocket is at " + server_.path_);
      return;
    }
    // 핸드셰이크 중인 연결도 센다: 등록(on_accept)까지 기다리면 동시에 들어온 요청들이 모두 통과한다
    if (server_.sessions_.size() + server_.handshaking_ >= server_.limits_.max_sessions) {
      reject(http::status::service_unavailable, "too many HMI sessions");
      return;
    }
    ++server_.handshaking_;
    beast::get_lowest_layer(ws_).expires_never();  // 이후 시간 제한은 WebSocket ping/pong 설정이 맡는다
    // max_lag의 절반마다 ping을 보내고, max_lag 안에 HMI에게서 아무것도(pong 포함) 못 받으면 끊는다.
    // 읽지 않는 HMI는 쌓인 데이터 뒤의 ping을 읽지 못해 pong도 못 보내므로, TCP 버퍼 크기와 무관하게
    // max_lag 안에 끊긴다. 듣기만 하는 정상 HMI는 WebSocket 구현이 pong을 자동으로 보낸다.
    websocket::stream_base::timeout opt{};
    opt.handshake_timeout = kHandshakeTimeout;
    opt.idle_timeout = server_.limits_.max_lag;
    opt.keep_alive_pings = true;
    ws_.set_option(opt);
    ws_.read_message_max(kMaxIncomingMessage);
    ws_.async_accept(request_, [self = shared_from_this()](beast::error_code accept_ec) {
      self->on_accept(accept_ec);
    });
  }

  void reject(http::status status, const std::string& reason) {
    server_.cb_.log("ws reject " + std::to_string(static_cast<unsigned>(status)) + ": " + reason);
    auto res = std::make_shared<http::response<http::string_body>>(status, request_.version());
    res->set(http::field::content_type, "text/plain");
    res->body() = reason + "\n";
    res->prepare_payload();
    res->keep_alive(false);
    http::async_write(ws_.next_layer(), *res, [self = shared_from_this(), res](beast::error_code, std::size_t) {
      beast::error_code ignored;
      beast::get_lowest_layer(self->ws_).socket().shutdown(tcp::socket::shutdown_send, ignored);
    });
  }

  void on_accept(beast::error_code ec) {
    --server_.handshaking_;
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

WsServer::WsServer(asio::io_context& io, const tcp::endpoint& endpoint, std::string path, Limits limits,
                   Callbacks callbacks)
    : acceptor_(io, endpoint), retry_timer_(io), path_(std::move(path)), limits_(limits), cb_(std::move(callbacks)) {
  accept();
}

WsServer::~WsServer() {
  beast::error_code ignored;
  acceptor_.close(ignored);
  retry_timer_.cancel();
}

void WsServer::broadcast(const Text& text) {
  // 복사본을 돈다: send()가 느린 세션을 끊으면 sessions_에서 지워져 원래 집합의 반복자가 무효가 된다.
  const std::vector<std::shared_ptr<Session>> targets(sessions_.begin(), sessions_.end());
  for (const auto& s : targets) s->send(text);
}

std::uint16_t WsServer::port() const { return acceptor_.local_endpoint().port(); }

void WsServer::accept() {
  acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
    if (ec == asio::error::operation_aborted) return;  // 종료 중
    if (ec) {
      // fd 고갈(EMFILE) 등은 곧바로 다시 받아도 같은 오류다. 기록하고 잠시 쉰다.
      cb_.log("ws accept error: " + ec.message());
      retry_timer_.expires_after(kAcceptRetryDelay);
      retry_timer_.async_wait([this](beast::error_code wait_ec) {
        if (!wait_ec) accept();
      });
      return;
    }
    std::make_shared<Session>(*this, std::move(socket))->start();
    accept();
  });
}

}  // namespace ugvc::app
