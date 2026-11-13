#pragma once

// IF-03 HMI → GW 명령 (ICD 7.2). 외부 입력의 경계: 여기서 검증을 끝내고 안쪽은 타입을 믿는다.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "ugvc/codec/command.hpp"

namespace ugvc::if03 {

inline constexpr std::size_t kMaxReqIdLen = 64;
inline constexpr std::size_t kMaxCommandTextLen = 4096;

struct CommandRequest {
  std::string req_id;
  codec::CmdType type = codec::CmdType::Stop;
  std::int32_t lat = 0;  // 1e-7 deg. STOP·RTB는 0
  std::int32_t lon = 0;
};

struct CommandParseError {
  std::optional<std::string> req_id;  // 유효한 req_id를 읽었을 때만. 있으면 HMI에 REJECTED_INVALID를 돌려준다
  std::string reason;                 // 진단용
};

std::variant<CommandRequest, CommandParseError> parse_command(std::string_view text);

}  // namespace ugvc::if03
