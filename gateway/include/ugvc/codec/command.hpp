#pragma once

// COMMAND (msg_type 0x10, payload 13B) — ICD 6.3. GW → SIM 방향이라 encode만 있다.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "ugvc/codec/bytes.hpp"
#include "ugvc/codec/msg_type.hpp"

namespace ugvc::codec {

inline constexpr std::size_t kCommandPayloadSize = 13;

// 표 6-4 cmd_type
enum class CmdType : std::uint8_t {
  Stop = 0x01,
  Rtb = 0x02,
  Waypoint = 0x03,
};

// IF-03 7.2의 이름과 같다
constexpr const char* name_of(CmdType t) {
  switch (t) {
    case CmdType::Stop: return "STOP";
    case CmdType::Rtb: return "RTB";
    case CmdType::Waypoint: return "WAYPOINT";
  }
  return nullptr;
}

// 정확히 일치하는 이름만 받는다 (대소문자 구분). 모르는 이름이면 nullopt
constexpr std::optional<CmdType> parse_cmd_type(std::string_view s) {
  for (const auto t : {CmdType::Stop, CmdType::Rtb, CmdType::Waypoint}) {
    if (s == name_of(t)) return t;
  }
  return std::nullopt;
}

struct Command {
  std::uint32_t cmd_id = 0;
  std::uint8_t cmd_type = 0;  // CmdType
  std::int32_t lat = 0;       // 1e-7 deg. STOP·RTB는 0
  std::int32_t lon = 0;

  bool operator==(const Command& o) const {
    return cmd_id == o.cmd_id && cmd_type == o.cmd_type && lat == o.lat && lon == o.lon;
  }
};

Bytes encode_command(const Command& c);

}  // namespace ugvc::codec
