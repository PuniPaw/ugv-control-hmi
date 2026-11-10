#pragma once

// msg_type 값 (ICD 5장 메시지 목록). 모든 C++ 코드는 여기 정의만 쓴다.

#include <cstdint>

namespace ugvc::codec {

inline constexpr std::uint8_t kMsgTelemetry = 0x01;  // SIM → GW, IF-01a
inline constexpr std::uint8_t kMsgDetection = 0x02;  // DET → GW, IF-02
inline constexpr std::uint8_t kMsgCommand = 0x10;    // GW → SIM, IF-01b
inline constexpr std::uint8_t kMsgAck = 0x11;        // SIM → GW, IF-01a
inline constexpr std::uint8_t kMsgHeartbeat = 0x20;  // GW → SIM, IF-01b

}  // namespace ugvc::codec
