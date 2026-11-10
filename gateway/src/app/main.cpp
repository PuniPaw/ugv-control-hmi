// ugvc_gateway: 통신 게이트웨이 실행 파일
//
// - UDP 15001(IF-01a, SIM)·15002(IF-02, DET) 수신 → Gateway(검증 ①~⑨, 링크, 유실) → 유효 패킷 JSON 출력
// - 50ms 판정 주기: 링크 상태 재판정, HEARTBEAT 1Hz 송신(IF-01b, SIM 15010), stats·link_status 1Hz 출력
//
// 출력은 IF-03 JSON(ICD 7.1)을 한 줄에 하나씩 stdout에 쓴다 (JSON Lines, STP 2.5).
// IF-03 WebSocket이 생기면 같은 메시지를 그쪽으로 보낸다.

#include <asio.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <variant>

#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/core/clock.hpp"
#include "ugvc/core/overloaded.hpp"
#include "ugvc/gateway/gateway.hpp"
#include "ugvc/gateway/heartbeat.hpp"
#include "ugvc/if03/messages.hpp"
#include "line_output.hpp"

namespace {

using asio::ip::udp;
using namespace ugvc;
using ingress::Source;

constexpr auto kJudgePeriod = std::chrono::milliseconds(50);  // 링크 판정 주기 (SDD 가정값)

struct Options {
  std::uint16_t vehicle_port = 15001;
  std::uint16_t detector_port = 15002;
  std::string sim_host = "127.0.0.1";
  std::uint16_t sim_port = 15010;
};

// 1~65535 정수가 아니면 예외. static_cast만 하면 70001이 조용히 4465가 된다.
std::uint16_t parse_port(const std::string& key, const std::string& value) {
  std::size_t used = 0;
  unsigned long port = 0;
  try {
    port = std::stoul(value, &used);
  } catch (const std::exception&) {
    used = 0;
  }
  if (used == 0 || used != value.size() || port < 1 || port > 65535) {
    throw std::invalid_argument(key + " must be a port number 1-65535, got '" + value + "'");
  }
  return static_cast<std::uint16_t>(port);
}

Options parse_options(int argc, const char* const argv[]) {
  Options o;
  for (int i = 1; i < argc; i += 2) {
    const std::string key = argv[i];
    if (i + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[i + 1];
    if (key == "--vehicle-port") o.vehicle_port = parse_port(key, value);
    else if (key == "--detector-port") o.detector_port = parse_port(key, value);
    else if (key == "--sim-host") o.sim_host = value;
    else if (key == "--sim-port") o.sim_port = parse_port(key, value);
    else throw std::invalid_argument("unknown option: " + key);
  }
  return o;
}

// IP 주소와 호스트 이름(localhost 등) 모두 받는다. make_address는 IP 문자열만 받는다.
udp::endpoint resolve_v4(asio::io_context& io, const std::string& host, std::uint16_t port) {
  udp::resolver resolver(io);
  return *resolver.resolve(udp::v4(), host, std::to_string(port)).begin();
}

class LinkStatusPublisher final : public link::IEventSink {
 public:
  explicit LinkStatusPublisher(app::LineOutput& out) : out_(out) {}

  void on_link_transition(const link::LinkTransition& t) override {
    std::cerr << "link " << ingress::to_string(t.source) << ' ' << link::to_string(t.from) << " -> "
              << link::to_string(t.to) << '\n';
    out_.publish(if03::link_status(t.source, t.to, t.last_rx_epoch_ms, t.at_epoch_ms));
  }

 private:
  app::LineOutput& out_;
};

// 포트 하나에서 datagram을 비동기로 계속 받아 handler에 넘긴다.
class UdpReceiver {
 public:
  using Handler = std::function<void(const std::uint8_t*, std::size_t)>;

  UdpReceiver(asio::io_context& io, std::uint16_t port, Handler handler)
      : socket_(io, udp::endpoint(udp::v4(), port)), handler_(std::move(handler)) {
    receive();
  }

 private:
  void receive() {
    // 완료되면 람다가 호출되고, 처리 후 다시 receive()로 다음 datagram을 기다린다.
    socket_.async_receive_from(asio::buffer(buf_), sender_,
                               [this](const asio::error_code& ec, std::size_t n) {
                                 if (ec == asio::error::operation_aborted) return;  // 종료 중
                                 if (!ec) handler_(buf_.data(), n);
                                 else std::cerr << "recv error: " << ec.message() << '\n';
                                 receive();
                               });
  }

  udp::socket socket_;
  udp::endpoint sender_;
  Handler handler_;
  // 최대보다 1 크게: 1200B를 넘는 datagram은 잘려서 1201B로 들어오고, 규칙 ①(길이 ≤ 1200)에서 length로 폐기된다
  std::array<std::uint8_t, codec::kMaxFrameSize + 1> buf_{};
};

class GatewayApp {
 public:
  GatewayApp(asio::io_context& io, const Options& o)
      : out_(io),
        events_(out_),
        gateway_(clock_, events_),
        heartbeat_(clock_),
        stats_period_(clock_, std::chrono::seconds(1)),
        vehicle_(io, o.vehicle_port, [this](auto* d, auto n) { on_datagram(Source::Vehicle, d, n); }),
        detector_(io, o.detector_port, [this](auto* d, auto n) { on_datagram(Source::Detector, d, n); }),
        sim_socket_(io, udp::endpoint(udp::v4(), 0)),
        sim_endpoint_(resolve_v4(io, o.sim_host, o.sim_port)),
        timer_(io, asio::steady_timer::clock_type::now()) {
    std::cerr << "ugvc_gateway: vehicle UDP " << o.vehicle_port << ", detector UDP " << o.detector_port
              << ", heartbeat -> " << o.sim_host << ':' << o.sim_port << '\n';
    tick();
  }

 private:
  void on_datagram(Source source, const std::uint8_t* data, std::size_t len) {
    const std::uint64_t rx_ts = clock_.epoch_ms();
    const auto frame = gateway_.on_datagram(source, data, len);
    if (!frame) return;
    std::visit(core::overloaded{
                   [&](const codec::Telemetry& t) { out_.publish(if03::telemetry(frame->header, t, rx_ts)); },
                   [&](const codec::Detection& d) { out_.publish(if03::detection(d, rx_ts)); },
                   [](const codec::Ack&) {},  // 명령 재전송(REQ-CMD-002, 12월)에서 처리한다
               },
               frame->message);
  }

  void tick() {
    gateway_.poll();
    if (auto hb = heartbeat_.poll()) {
      asio::error_code ec;
      sim_socket_.send_to(asio::buffer(*hb), sim_endpoint_, 0, ec);
      if (ec) std::cerr << "heartbeat send error: " << ec.message() << '\n';
    }
    if (stats_period_.due()) {
      const std::uint64_t now = clock_.epoch_ms();
      for (Source s : {Source::Vehicle, Source::Detector}) {
        out_.publish(if03::stats(s, gateway_.stats(s), now));
        const auto& link = gateway_.link(s);
        out_.publish(if03::link_status(s, link.state(), link.last_rx_epoch_ms(), now));
      }
      if (out_.dropped() != reported_drops_) {
        std::cerr << "stdout reader is behind: dropped " << out_.dropped() - reported_drops_ << " lines\n";
        reported_drops_ = out_.dropped();
      }
    }
    // 직전 예정 시각 + 50ms: 처리 시간만큼 판정 주기가 조금씩 밀리지 않게 한다
    timer_.expires_at(timer_.expiry() + kJudgePeriod);
    timer_.async_wait([this](const asio::error_code& ec) {
      if (!ec) tick();
    });
  }

  core::SystemClock clock_;
  app::LineOutput out_;  // events_보다 먼저 생성되어야 한다 (멤버는 선언 순서대로 생성)
  LinkStatusPublisher events_;
  std::uint64_t reported_drops_ = 0;
  gateway::Gateway gateway_;
  gateway::HeartbeatSender heartbeat_;
  core::Periodic stats_period_;
  UdpReceiver vehicle_;
  UdpReceiver detector_;
  udp::socket sim_socket_;
  udp::endpoint sim_endpoint_;
  asio::steady_timer timer_;
};

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
#if defined(ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
    std::signal(SIGPIPE, SIG_IGN);  // stdout을 읽던 쪽이 끝나도 프로세스가 죽지 않고 쓰기 오류로 받는다
#endif
    asio::io_context io;
    GatewayApp app(io, options);

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const asio::error_code&, int) { io.stop(); });

    io.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
