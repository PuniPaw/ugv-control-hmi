// 시제품 A: Boost.Beast WebSocket 서버를 GW와 같은 단일 스레드 io_context에 붙인다.
// 100ms마다 JSON을 모든 클라이언트에 브로드캐스트하고, 클라이언트 텍스트 메시지를 받아 출력한다.
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = net::ip::tcp;

class Session;
std::set<std::shared_ptr<Session>> g_sessions;

class Session : public std::enable_shared_from_this<Session> {
 public:
  explicit Session(tcp::socket s) : ws_(std::move(s)) {}
  void start() {
    ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
      if (ec) return;
      g_sessions.insert(self);
      self->read();
    });
  }
  void send(std::shared_ptr<const std::string> msg) {
    if (queue_.size() >= 1000) return;  // 느린 클라이언트: 버린다
    queue_.push_back(std::move(msg));
    if (queue_.size() == 1) write();
  }

 private:
  void read() {
    ws_.async_read(buf_, [self = shared_from_this()](beast::error_code ec, std::size_t) {
      if (ec) { g_sessions.erase(self); return; }
      std::cout << "rx: " << beast::buffers_to_string(self->buf_.data()) << std::endl;
      self->buf_.consume(self->buf_.size());
      self->read();
    });
  }
  void write() {
    ws_.text(true);
    ws_.async_write(net::buffer(*queue_.front()), [self = shared_from_this()](beast::error_code ec, std::size_t) {
      if (ec) { g_sessions.erase(self); return; }
      self->queue_.pop_front();
      if (!self->queue_.empty()) self->write();
    });
  }
  ws::stream<tcp::socket> ws_;
  beast::flat_buffer buf_;
  std::deque<std::shared_ptr<const std::string>> queue_;
};

void accept(tcp::acceptor& a) {
  a.async_accept([&a](beast::error_code ec, tcp::socket s) {
    if (!ec) std::make_shared<Session>(std::move(s))->start();
    accept(a);
  });
}

int main() {
  net::io_context io;
  tcp::acceptor acceptor(io, {tcp::v4(), 18080});
  accept(acceptor);
  net::steady_timer timer(io, std::chrono::steady_clock::now());
  int n = 0;
  std::function<void()> tick = [&] {
    auto msg = std::make_shared<const std::string>("{\"type\":\"telemetry\",\"seq\":" + std::to_string(n++) + "}");
    for (auto& s : g_sessions) s->send(msg);
    timer.expires_at(timer.expiry() + std::chrono::milliseconds(100));
    timer.async_wait([&](beast::error_code) { tick(); });
  };
  tick();
  io.run();
}
