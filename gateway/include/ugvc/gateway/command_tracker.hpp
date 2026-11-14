#pragma once

// 명령 추적 (REQ-CMD-002/003/005). 소켓 없이 시계만 주입받는 순수 상태 기계:
// 보낼 명령을 반환하고 ACK·시간 경과를 받아 결과를 확정한다. 실제 송신은 호출자(main)가 한다.
//
// 한 명령은 최대 4번(최초 1 + 재전송 3) 같은 cmd_id로 보낸다. 마지막 송신 후 1.0s 안에 ACK가 없으면 TIMEOUT.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/command.hpp"
#include "ugvc/core/clock.hpp"
#include "ugvc/if03/command.hpp"

namespace ugvc::gateway {

inline constexpr std::chrono::milliseconds kAckTimeout{1000};
inline constexpr int kMaxRetransmits = 3;

// IF-03 cmd_result.result (ICD 7.1). ACK 결과 3종 + GW가 정하는 TIMEOUT
enum class CmdResult : std::uint8_t { Accepted, RejectedInvalid, RejectedState, Timeout };

constexpr const char* name_of(CmdResult r) {
  switch (r) {
    case CmdResult::Accepted: return "ACCEPTED";
    case CmdResult::RejectedInvalid: return "REJECTED_INVALID";
    case CmdResult::RejectedState: return "REJECTED_STATE";
    case CmdResult::Timeout: return "TIMEOUT";
  }
  return nullptr;
}

struct CommandSent {
  std::string req_id;
  std::uint32_t cmd_id;
  codec::CmdType cmd_type;
  int attempt;  // 1 = 최초 송신
};

struct CommandResult {
  std::string req_id;
  std::optional<std::uint32_t> cmd_id;  // 파싱 단계에서 거부돼 번호를 받지 못한 명령은 없음
  CmdResult result;
  int attempts;  // 실제 송신 횟수
};

// 대기 중이 아닌 cmd_id의 ACK (늦은·중복·모르는 번호). 기록만 한다.
struct UnknownAck {
  std::uint32_t cmd_id;
  std::uint8_t result;  // 받은 그대로
};

using CommandEvent = std::variant<CommandSent, CommandResult, UnknownAck>;

class CommandTracker {
 public:
  // first_cmd_id는 cmd_id 순환 시험용. 0은 "없음"으로 예약되어 있어 1로 바꾼다.
  explicit CommandTracker(const core::IClock& clock, std::uint32_t first_cmd_id = 1)
      : clock_(clock), next_id_(first_cmd_id == 0 ? 1 : first_cmd_id) {}

  // 새 명령이면 지금 보낼 COMMAND(1번째 송신). 같은 req_id가 이미 대기 중이면 nullopt (재제출은 무시).
  std::optional<codec::Command> submit(const if03::CommandRequest& req);

  void on_ack(const codec::Ack& ack);

  // 지금 다시 보낼 명령. 재전송 한도를 넘긴 명령은 TIMEOUT으로 확정하고 목록에서 뺀다.
  std::vector<codec::Command> poll();

  std::vector<CommandEvent> take_events() { return std::exchange(events_, {}); }

 private:
  struct Pending {
    std::string req_id;
    codec::Command command;
    int sends;
    core::SteadyTime last_send;
  };

  std::uint32_t issue_id();

  const core::IClock& clock_;
  std::uint32_t next_id_;
  std::vector<Pending> pending_;  // 제출 순서. 사건도 이 순서로 나온다
  std::vector<CommandEvent> events_;
};

}  // namespace ugvc::gateway
