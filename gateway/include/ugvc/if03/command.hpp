#pragma once

// IF-03 HMI → GW 명령 (ICD 7.2). 외부 입력의 경계: 여기서 검증을 끝내고 안쪽은 타입을 믿는다.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "ugvc/codec/command.hpp"
#include "ugvc/gateway/command_tracker.hpp"

namespace ugvc::if03 {

inline constexpr std::size_t kMaxReqIdLen = 64;  // 바이트(UTF-8)
inline constexpr std::size_t kMaxCommandTextLen = 4096;

// 명령 요청 타입은 gateway가 가진다 (if03이 gateway를 쓰고 그 반대는 아니다, SDD 2장)
using CommandRequest = gateway::CommandRequest;

struct CommandParseError {
  std::optional<std::string> req_id;  // 유효한 req_id를 읽었을 때만. 있으면 CommandTracker::reject로 넘긴다 (응답 여부는 tracker가 정함)
  std::string reason;                 // 진단용
};

std::variant<CommandRequest, CommandParseError> parse_command(std::string_view text);

}  // namespace ugvc::if03
