// ugvc_gateway: IF-01a(UDP 15001)로 TELEMETRY를 받아 콘솔에 JSON 한 줄씩 출력한다.
//
// 현재 단계(11/10 주): 수신 → 헤더·CRC 확인 → TELEMETRY 디코딩 → 출력.
// 검증 규칙 9단계 전체, 링크 상태, stats 출력은 11/17 주에 별도 모듈로 분리해 추가한다.
// 출력 형식은 IF-03 telemetry 메시지(ICD 7.1)와 같은 모양으로 맞춰 둔다.

#include <asio.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ugvc/codec/crc16.hpp"
#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/telemetry.hpp"

namespace {

using asio::ip::udp;
using namespace ugvc::codec;

constexpr std::uint16_t kDefaultVehiclePort = 15001;

std::uint64_t now_epoch_ms() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

const char* mode_name(std::uint8_t mode) {
  switch (static_cast<Mode>(mode)) {
    case Mode::Standby: return "STANDBY";
    case Mode::Mission: return "MISSION";
    case Mode::Stopped: return "STOPPED";
    case Mode::Rtb: return "RTB";
    case Mode::Fault: return "FAULT";
  }
  return "UNKNOWN";
}

nlohmann::json flag_names(std::uint16_t f) {
  nlohmann::json out = nlohmann::json::array();
  if (f & flags::kGpsValid) out.push_back("GPS_VALID");
  if (f & flags::kCameraOk) out.push_back("CAMERA_OK");
  if (f & flags::kBatteryLow) out.push_back("BATTERY_LOW");
  if (f & flags::kFailsafeActive) out.push_back("FAILSAFE_ACTIVE");
  return out;  // 예약 비트(4–15)는 무시 (ICD 3장)
}

// 정수 LSB → 공학 단위 (ADR-002: 변환은 IF-03 경계에서만)
nlohmann::json telemetry_json(const Header& h, const Telemetry& t, std::uint64_t rx_ts) {
  return {
      {"type", "telemetry"},
      {"ts", rx_ts},
      {"data",
       {
           {"lat", t.lat * 1e-7},
           {"lon", t.lon * 1e-7},
           {"heading_deg", t.heading * 0.01},
           {"speed_mps", t.speed * 0.01},
           {"battery_pct", t.battery},
           {"mode", mode_name(t.mode)},
           {"flags", flag_names(t.status_flags)},
           {"seq", h.seq},
           {"rx_ts", rx_ts},
       }},
  };
}

// 임시 최소 검사 (규칙 ①~⑤). 실패 사유 문자열, 통과하면 nullptr.
const char* basic_check(const std::uint8_t* data, std::size_t len, Header& h) {
  if (len < kFrameOverhead) return "length";
  h = *decode_header(data, len);
  if (h.sync != kSync) return "sync";
  if (h.version != kVersion) return "version";
  if (len != kFrameOverhead + h.payload_len) return "length";
  if (*read_frame_crc(data, len) != crc16_ccitt_false(data, len - kCrcSize)) return "crc";
  return nullptr;
}

class VehicleReceiver {
 public:
  VehicleReceiver(asio::io_context& io, std::uint16_t port)
      : socket_(io, udp::endpoint(udp::v4(), port)) {
    receive();
  }

 private:
  void receive() {
    // 비동기 수신: 완료되면 람다가 호출되고, 처리 후 다시 receive()로 다음 패킷을 기다린다.
    socket_.async_receive_from(
        asio::buffer(buf_), sender_, [this](const asio::error_code& ec, std::size_t n) {
          if (!ec) {
            handle(buf_.data(), n);
          } else if (ec == asio::error::operation_aborted) {
            return;  // 종료 중
          } else {
            std::cerr << "recv error: " << ec.message() << '\n';
          }
          receive();
        });
  }

  void handle(const std::uint8_t* data, std::size_t len) {
    const std::uint64_t rx_ts = now_epoch_ms();
    Header h;
    if (const char* reason = basic_check(data, len, h)) {
      std::cerr << "reject " << reason << " (" << len << " bytes from " << sender_ << ")\n";
      return;
    }
    if (h.msg_type != kMsgTelemetry) {
      std::cerr << "skip msg_type 0x" << std::hex << int{h.msg_type} << std::dec << '\n';
      return;
    }
    const auto t = decode_telemetry(data + kHeaderSize, h.payload_len);
    if (!t) {
      std::cerr << "reject length (telemetry payload " << h.payload_len << ")\n";
      return;
    }
    // 한 줄에 JSON 하나 (JSON Lines). 파이프로 읽는 시험을 위해 줄마다 flush한다.
    std::cout << telemetry_json(h, *t, rx_ts).dump() << std::endl;
  }

  udp::socket socket_;
  udp::endpoint sender_;
  std::array<std::uint8_t, kMaxFrameSize + 1> buf_{};  // 최대보다 1 크게: 초과 패킷 감지용
};

}  // namespace

int main(int argc, char* argv[]) {
  std::uint16_t port = kDefaultVehiclePort;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--vehicle-port") {
      port = static_cast<std::uint16_t>(std::stoul(argv[i + 1]));
    }
  }

  try {
    asio::io_context io;
    VehicleReceiver vehicle(io, port);

    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const asio::error_code&, int) { io.stop(); });

    std::cerr << "ugvc_gateway: listening UDP " << port << " (IF-01a)\n";
    io.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
