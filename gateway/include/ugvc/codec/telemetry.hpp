#pragma once

// TELEMETRY (msg_type 0x01, payload 16B) — ICD 6.1
//
// 필드는 ICD의 정수 값(LSB 단위)을 그대로 담는다. 공학 단위(deg, m/s) 변환은
// GW → HMI(IF-03) 경계에서만 한다 (ADR-002). 범위 검사는 검증 규칙 ⑧에서 한다.

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ugvc/codec/bytes.hpp"
#include "ugvc/codec/msg_type.hpp"

namespace ugvc::codec {

inline constexpr std::size_t kTelemetryPayloadSize = 16;

// 표 6-1 mode
enum class Mode : std::uint8_t {
  Standby = 0,
  Mission = 1,
  Stopped = 2,
  Rtb = 3,
  Fault = 4,
};

// 표 6-1 이름. 정의되지 않은 값이면 nullptr (검증 ⑧과 IF-03 변환이 함께 쓴다)
constexpr const char* name_of(Mode m) {
  switch (m) {
    case Mode::Standby: return "STANDBY";
    case Mode::Mission: return "MISSION";
    case Mode::Stopped: return "STOPPED";
    case Mode::Rtb: return "RTB";
    case Mode::Fault: return "FAULT";
  }
  return nullptr;
}

// 표 6-1 범위 (단위는 각 필드의 LSB)
inline constexpr std::int32_t kLatLimit = 900'000'000;    // ±90°, 1e-7 deg
inline constexpr std::int32_t kLonLimit = 1'800'000'000;  // ±180°, 1e-7 deg
inline constexpr std::uint16_t kHeadingMax = 35'999;      // 359.99°, 0.01 deg
inline constexpr std::uint16_t kSpeedMax = 3'000;         // 30 m/s, 0.01 m/s
inline constexpr std::uint8_t kBatteryMax = 100;          // %

// 표 6-2 status_flags 비트
namespace flags {
inline constexpr std::uint16_t kGpsValid = 1U << 0;
inline constexpr std::uint16_t kCameraOk = 1U << 1;
inline constexpr std::uint16_t kBatteryLow = 1U << 2;
inline constexpr std::uint16_t kFailsafeActive = 1U << 3;
inline constexpr std::uint16_t kDefinedMask = 0x000F;  // 4–15는 예약
}  // namespace flags

struct Telemetry {
  std::int32_t lat = 0;            // 1e-7 deg
  std::int32_t lon = 0;            // 1e-7 deg
  std::uint16_t heading = 0;       // 0.01 deg
  std::uint16_t speed = 0;         // 0.01 m/s
  std::uint8_t battery = 0;        // 1 %
  std::uint8_t mode = 0;           // Mode. 범위 밖 값도 받은 그대로 둔다 (검증 ⑧에서 판정)
  std::uint16_t status_flags = 0;  // flags::*

  bool operator==(const Telemetry& o) const {
    return lat == o.lat && lon == o.lon && heading == o.heading && speed == o.speed &&
           battery == o.battery && mode == o.mode && status_flags == o.status_flags;
  }
};

Bytes encode_telemetry(const Telemetry& t);

// payload 길이가 16이 아니면 nullopt.
std::optional<Telemetry> decode_telemetry(const std::uint8_t* payload, std::size_t len) noexcept;

}  // namespace ugvc::codec
