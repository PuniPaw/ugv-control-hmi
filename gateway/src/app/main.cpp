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

#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/core/clock.hpp"
#include "ugvc/gateway/gateway.hpp"
#include "ugvc/gateway/heartbeat.hpp"
#include "ugvc/if03/messages.hpp"

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

Options parse_options(int argc, char* argv[]) {
  Options o;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string key = argv[i];
    const std::string value = argv[i + 1];
    if (key == "--vehicle-port") o.vehicle_port = static_cast<std::uint16_t>(std::stoul(value));
    else if (key == "--detector-port") o.detector_port = static_cast<std::uint16_t>(std::stoul(value));
    else if (key == "--sim-host") o.sim_host = value;
    else if (key == "--sim-port") o.sim_port = static_cast<std::uint16_t>(std::stoul(value));
    else throw std::invalid_argument("unknown option: " + key);
  }
  return o;
}

void emit(const nlohmann::json& msg) {
  std::cout << msg.dump() << std::endl;  // 줄마다 flush: 파이프로 읽는 시험을 위해
}

class StdoutEventSink final : public link::IEventSink {
 public:
  void on_link_transition(const link::LinkTransition& t) override {
    std::cerr << "link " << ingress::to_string(t.source) << ' ' << link::to_string(t.from) << " -> "
              << link::to_string(t.to) << '\n';
    emit(if03::link_status(t.source, t.to, t.last_rx_epoch_ms, t.at_epoch_ms));
  }
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
  std::array<std::uint8_t, codec::kMaxFrameSize + 1> buf_{};  // 최대보다 1 크게: 초과 datagram이 잘리지 않고 ④에서 걸린다
};

class GatewayApp {
 public:
  GatewayApp(asio::io_context& io, const Options& o)
      : gateway_(clock_, events_),
        heartbeat_(clock_),
        stats_period_(clock_, std::chrono::seconds(1)),
        vehicle_(io, o.vehicle_port, [this](auto* d, auto n) { on_datagram(Source::Vehicle, d, n); }),
        detector_(io, o.detector_port, [this](auto* d, auto n) { on_datagram(Source::Detector, d, n); }),
        sim_socket_(io, udp::endpoint(udp::v4(), 0)),
        sim_endpoint_(asio::ip::make_address(o.sim_host), o.sim_port),
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
    const auto& h = frame->header;
    if (h.msg_type == codec::kMsgTelemetry) {
      emit(if03::telemetry(h, *codec::decode_telemetry(frame->payload, h.payload_len), rx_ts));
    } else if (h.msg_type == codec::kMsgDetection) {
      emit(if03::detection(*codec::decode_detection(frame->payload, h.payload_len), rx_ts));
    }
    // ACK는 명령 재전송(REQ-CMD-002, 12월)에서 처리한다
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
        emit(if03::stats(s, gateway_.stats(s), now));
        const auto& link = gateway_.link(s);
        emit(if03::link_status(s, link.state(), link.last_rx_epoch_ms(), now));
      }
    }
    // 직전 예정 시각 + 50ms: 처리 시간만큼 판정 주기가 조금씩 밀리지 않게 한다
    timer_.expires_at(timer_.expiry() + kJudgePeriod);
    timer_.async_wait([this](const asio::error_code& ec) {
      if (!ec) tick();
    });
  }

  core::SystemClock clock_;
  StdoutEventSink events_;
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
