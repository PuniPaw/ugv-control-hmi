// ugvc_gateway: 통신 게이트웨이 실행 파일
//
// - UDP 15001(IF-01a, SIM)·15002(IF-02, DET) 수신 → Gateway(검증 ①~⑨, 링크, 유실) → 유효 패킷 JSON 출력
// - 50ms 판정 주기: 링크 상태 재판정, HEARTBEAT 1Hz 송신(IF-01b, SIM 15010), stats·link_status 1Hz 출력
//
// IF-03 JSON(ICD 7.1)은 같은 메시지를 두 곳에 보낸다: WebSocket(기본 127.0.0.1:8080 /ws, HMI)과
// stdout(한 줄에 하나, JSON Lines, 통합 시험·기록용). REQ-COM-008.
// 진단(링크 전이, 오류, 버림 보고)은 stderr에 쓴다. 둘 다 LineSink를 거쳐 io 스레드를 막지 않는다.

#include <boost/asio.hpp>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/core/clock.hpp"
#include "ugvc/core/overloaded.hpp"
#include "ugvc/core/periodic.hpp"
#include "ugvc/gateway/gateway.hpp"
#include "ugvc/gateway/heartbeat.hpp"
#include "ugvc/if03/messages.hpp"
#include "line_sink.hpp"
#include "ws_server.hpp"

namespace asio = boost::asio;

namespace {

using asio::ip::udp;
using namespace ugvc;
using ingress::Source;

constexpr auto kJudgePeriod = std::chrono::milliseconds(50);  // 링크 판정 주기 (SDD 가정값)
constexpr std::size_t kMaxStdoutLines = 1000;  // 약 1~2초 분량. 넘치면 표시용 줄만 잃는다
constexpr std::size_t kMaxStderrLines = 200;   // 진단은 드물다. 이만큼 밀리면 읽는 쪽이 멈춘 것
constexpr std::size_t kMaxWsQueued = 1000;     // HMI 세션마다. 10Hz 텔레메트리 + 탐지 기준 약 1~2초 분량
constexpr const char* kWsPath = "/ws";         // ICD 2장 IF-03

struct Options {
  std::uint16_t vehicle_port = 15001;
  std::uint16_t detector_port = 15002;
  std::string sim_host = "127.0.0.1";
  std::uint16_t sim_port = 15010;
  std::string ws_host = "127.0.0.1";  // 다른 PC의 HMI를 받으려면 0.0.0.0
  std::uint16_t ws_port = 8080;
};

// 1~65535 정수가 아니면 예외. static_cast만 하면 70001이 조용히 4465가 된다.
// allow_any: 0("OS가 빈 포트를 고른다")도 받는다. 실제 포트는 시작 메시지에 찍힌다 (시험용).
std::uint16_t parse_port(const std::string& key, const std::string& value, bool allow_any = false) {
  std::size_t used = 0;
  unsigned long port = 0;
  try {
    port = std::stoul(value, &used);
  } catch (const std::exception&) {
    used = 0;
  }
  const unsigned long min = allow_any ? 0 : 1;
  if (used == 0 || used != value.size() || port < min || port > 65535) {
    throw std::invalid_argument(key + " must be a port number " + std::to_string(min) + "-65535, got '" + value + "'");
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
    else if (key == "--ws-host") o.ws_host = value;
    else if (key == "--ws-port") o.ws_port = parse_port(key, value, /*allow_any=*/true);
    else throw std::invalid_argument("unknown option: " + key);
  }
  return o;
}

// IP 주소와 호스트 이름(localhost 등) 모두 받는다. make_address는 IP 문자열만 받는다.
template <typename Protocol>
typename Protocol::endpoint resolve_v4(asio::io_context& io, const std::string& host, std::uint16_t port) {
  typename Protocol::resolver resolver(io);
  return *resolver.resolve(Protocol::v4(), host, std::to_string(port)).begin();
}

// 진단 한 줄을 만든다: diag(sink, "a ", 1) → "a 1"
template <typename... Parts>
void diag(app::LineSink& sink, const Parts&... parts) {
  std::ostringstream os;
  (os << ... << parts);
  sink.publish(os.str());
}

// 포트 하나에서 datagram을 비동기로 계속 받아 handler에 넘긴다.
class UdpReceiver {
 public:
  using Handler = std::function<void(const std::uint8_t*, std::size_t)>;

  UdpReceiver(asio::io_context& io, std::uint16_t port, app::LineSink& log, Handler handler)
      : socket_(io, udp::endpoint(udp::v4(), port)), log_(log), handler_(std::move(handler)) {
    receive();
  }

 private:
  void receive() {
    // 완료되면 람다가 호출되고, 처리 후 다시 receive()로 다음 datagram을 기다린다.
    socket_.async_receive_from(asio::buffer(buf_), sender_,
                               [this](const boost::system::error_code& ec, std::size_t n) {
                                 if (ec == asio::error::operation_aborted) return;  // 종료 중
                                 if (!ec) handler_(buf_.data(), n);
                                 else diag(log_, "recv error: ", ec.message());
                                 receive();
                               });
  }

  udp::socket socket_;
  udp::endpoint sender_;
  app::LineSink& log_;
  Handler handler_;
  // 최대보다 1 크게: 1200B를 넘는 datagram은 잘려서 1201B로 들어오고, 규칙 ①(길이 ≤ 1200)에서 length로 폐기된다
  std::array<std::uint8_t, codec::kMaxFrameSize + 1> buf_{};
};

class GatewayApp {
 public:
  GatewayApp(asio::io_context& io, const Options& o)
      : out_(STDOUT_FILENO, kMaxStdoutLines),
        log_(STDERR_FILENO, kMaxStderrLines),
        gateway_(clock_),
        heartbeat_(clock_),
        stats_period_(clock_, std::chrono::seconds(1)),
        vehicle_(io, o.vehicle_port, log_, [this](auto* d, auto n) { on_datagram(Source::Vehicle, d, n); }),
        detector_(io, o.detector_port, log_, [this](auto* d, auto n) { on_datagram(Source::Detector, d, n); }),
        sim_socket_(io, udp::endpoint(udp::v4(), 0)),
        sim_endpoint_(resolve_v4<udp>(io, o.sim_host, o.sim_port)),
        ws_(io, resolve_v4<asio::ip::tcp>(io, o.ws_host, o.ws_port), kWsPath, kMaxWsQueued,
            {[this] { return snapshot_texts(); },
             [this](const std::string& m) { on_hmi_message(m); },
             [this](const std::string& line) { log_.publish(line); }}),
        timer_(io, asio::steady_timer::clock_type::now()) {
    diag(log_, "ugvc_gateway: vehicle UDP ", o.vehicle_port, ", detector UDP ", o.detector_port,
         ", heartbeat -> ", o.sim_host, ':', o.sim_port, ", IF-03 ws://", o.ws_host, ':', ws_.port(), kWsPath);
    tick();
  }

 private:
  void on_datagram(Source source, const std::uint8_t* data, std::size_t len) {
    const auto accepted = gateway_.on_datagram(source, data, len);
    publish_transitions();  // 패킷 처리 중 전이(LOST → CONNECTED 등)가 telemetry보다 먼저 나간다
    if (!accepted) return;
    const auto& [frame, rx_ts] = *accepted;
    std::visit(core::overloaded{
                   [&](const codec::Telemetry& t) { publish(if03::telemetry(frame.header, t, rx_ts)); },
                   [&](const codec::Detection& d) { publish(if03::detection(d, rx_ts)); },
                   [](const codec::Ack&) {},  // 명령 재전송(REQ-CMD-002, 12월)에서 처리한다
               },
               frame.message);
  }

  void publish_transitions() {
    for (const auto& t : gateway_.take_transitions()) {
      diag(log_, "link ", ingress::to_string(t.source), ' ', link::to_string(t.change.from), " -> ",
           link::to_string(t.change.to));
      publish(if03::link_status(t));
    }
  }

  void tick() {
    gateway_.poll();
    publish_transitions();
    if (auto hb = heartbeat_.poll()) {
      boost::system::error_code ec;
      sim_socket_.send_to(asio::buffer(*hb), sim_endpoint_, 0, ec);
      if (ec) diag(log_, "heartbeat send error: ", ec.message());
    }
    if (stats_period_.due()) {
      for (const auto& msg : if03::snapshot(gateway_, clock_.epoch_ms())) publish(msg);
      report_drops();
    }
    // 직전 예정 시각 + 50ms: 처리 시간만큼 판정 주기가 조금씩 밀리지 않게 한다
    timer_.expires_at(timer_.expiry() + kJudgePeriod);
    timer_.async_wait([this](const boost::system::error_code& ec) {
      if (!ec) tick();
    });
  }

  // IF-03 메시지 하나를 직렬화는 한 번만 하고 stdout과 모든 HMI 세션에 보낸다.
  void publish(const nlohmann::json& msg) {
    auto text = std::make_shared<const std::string>(msg.dump());
    out_.publish(*text);
    ws_.broadcast(text);
  }

  // 새 HMI 세션에 먼저 보낼 현재 상태: 다음 1Hz 보고를 기다리지 않고 링크 상태를 그린다.
  std::vector<app::WsServer::Text> snapshot_texts() const {
    const auto msgs = if03::snapshot(gateway_, clock_.epoch_ms());
    std::vector<app::WsServer::Text> texts;
    std::transform(msgs.begin(), msgs.end(), std::back_inserter(texts),
                   [](const nlohmann::json& m) { return std::make_shared<const std::string>(m.dump()); });
    return texts;
  }

  // IF-03 7.2 명령(HMI → GW). 명령 송신·재전송은 12월(REQ-CMD-002) 작업이라 지금은 기록만 한다.
  void on_hmi_message(const std::string& m) {
    diag(log_, "ws message ignored until command relay (REQ-CMD-002): ", m.substr(0, 200));
  }

  // 버린 줄 수가 늘었으면 stderr에 알린다. stderr 자신이 버린 줄은 자리가 날 때 함께 알린다.
  void report_drops() {
    if (const auto d = out_.dropped(); d != reported_stdout_drops_) {
      diag(log_, "stdout reader is behind: dropped ", d - reported_stdout_drops_, " lines");
      reported_stdout_drops_ = d;
    }
    if (const auto d = log_.dropped(); d != reported_stderr_drops_) {
      diag(log_, "stderr reader is behind: dropped ", d - reported_stderr_drops_, " lines");
      reported_stderr_drops_ = d;
    }
    if (const auto d = ws_.dropped(); d != reported_ws_drops_) {
      diag(log_, "ws clients are behind: dropped ", d - reported_ws_drops_, " messages");
      reported_ws_drops_ = d;
    }
  }

  core::SystemClock clock_;
  app::LineSink out_;  // out_·log_는 이들을 참조하는 수신기보다 먼저 선언한다 (선언 순서대로 생성)
  app::LineSink log_;
  std::uint64_t reported_stdout_drops_ = 0;
  std::uint64_t reported_stderr_drops_ = 0;
  std::uint64_t reported_ws_drops_ = 0;
  gateway::Gateway gateway_;
  gateway::HeartbeatSender heartbeat_;
  core::Periodic stats_period_;
  UdpReceiver vehicle_;
  UdpReceiver detector_;
  udp::socket sim_socket_;
  udp::endpoint sim_endpoint_;
  app::WsServer ws_;
  asio::steady_timer timer_;
};

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    std::signal(SIGPIPE, SIG_IGN);  // 출력을 읽던 쪽이 끝나도 프로세스가 죽지 않고 쓰기 오류(EPIPE)로 받는다
    asio::io_context io;
    GatewayApp app(io, options);

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const boost::system::error_code&, int) { io.stop(); });

    io.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
