#pragma once

// 명령 추적 (REQ-CMD-002/003/005). 소켓 없이 시계만 주입받는 순수 상태 기계:
// 보낼 명령을 반환하고 ACK·시간 경과를 받아 결과를 확정한다. 실제 송신은 호출자(main)가 한다.
//
// 한 명령은 최대 4번(최초 1 + 재전송 3) 같은 cmd_id로 보낸다. 송신 예정 시각은 1.0s 간격(0·1·2·3s)이고
// 4번째 송신 예정 시각 + 1.0s(4.0s)까지 ACK가 없으면 TIMEOUT (ICD 7.3).
//
// req_id 규칙 (ICD 7.2): req_id는 GW 전체에서 한 명령을 가리킨다. 대기 중이거나 최근에 결과가 나간 req_id로
// 다시 오면 다시 보내지 않는다. 내용이 같고 결과가 나갔으면 같은 결과를 다시 알리고, 그 밖은 무시하고 기록한다.
// 형식 오류로 거절한 req_id도 기억한다: 다시 온 형식 오류에는 같은 거절을 다시 알리고, 올바른 명령은 무시한다.

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

namespace ugvc::gateway {

inline constexpr std::chrono::milliseconds kAckTimeout{1000};
inline constexpr int kMaxRetransmits = 3;
inline constexpr std::chrono::seconds kRecentResultTtl{60};  // 결과가 나간 req_id를 기억하는 시간
inline constexpr std::size_t kMaxRecentResults = 256;          // 그 개수 상한

// HMI 명령 한 건 (IF-03 7.2를 if03::parse_command가 검증한 결과)
struct CommandRequest {
  std::string req_id;
  codec::CmdType type = codec::CmdType::Stop;
  std::int32_t lat = 0;  // 1e-7 deg. STOP·RTB는 0
  std::int32_t lon = 0;

  bool same_command(const CommandRequest& o) const { return type == o.type && lat == o.lat && lon == o.lon; }
};

// cmd_id 시작값: 0.1초 단위 시각 (ICD 7.3). GW를 다시 시작해도 앞서 쓴 번호와 겹치지 않는다
// (명령이 초당 10개보다 드물다는 전제). 0은 "없음"으로 예약되어 1로 바꾼다.
std::uint32_t initial_cmd_id(std::uint64_t epoch_ms);

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
  std::optional<std::uint32_t> cmd_id;  // 형식 오류로 거부돼 번호를 받지 못한 명령은 없음
  CmdResult result;
  int attempts;          // 실제 송신 횟수
  bool repeated = false;  // 이미 나간 결과를 같은 req_id 재제출에 다시 알리는 것
};

// 보내지 않고 무시한 명령 (REQ-CMD-005: 모든 명령을 기록). 결과는 알리지 않는다
struct CommandIgnored {
  std::string req_id;
  const char* reason;  // "req_id pending", "req_id pending with different command", ...
};

// 대기 중이 아닌 cmd_id의 ACK (늦은·중복·모르는 번호). 기록만 한다.
struct UnknownAck {
  std::uint32_t cmd_id;
  std::uint8_t result;  // 받은 그대로
};

using CommandEvent = std::variant<CommandSent, CommandResult, UnknownAck, CommandIgnored>;

class CommandTracker {
 public:
  // cmd_id는 initial_cmd_id(시각)부터. first_cmd_id는 시험용 (0은 1로 바꾼다)
  explicit CommandTracker(const core::IClock& clock)
      : CommandTracker(clock, initial_cmd_id(clock.epoch_ms())) {}
  CommandTracker(const core::IClock& clock, std::uint32_t first_cmd_id)
      : clock_(clock), next_id_(first_cmd_id == 0 ? 1 : first_cmd_id) {}

  // 새 명령이면 지금 보낼 COMMAND(1번째 송신). 아니면 nullopt (req_id 규칙, 위 머리말)
  std::optional<codec::Command> submit(const CommandRequest& req);

  // 형식 오류로 보내지 않는 명령 (req_id만 읽음). 새 req_id면 REJECTED_INVALID 결과(cmd_id 없음, attempts 0)를
  // 내고 기억한다. 최근에 형식 오류로 거절한 req_id면 그 결과를 다시 알리고, 대기 중이거나 올바른 명령에
  // 쓰인 req_id면 무시만 한다
  void reject(const std::string& req_id);

  void on_ack(const codec::Ack& ack);

  // 지금 다시 보낼 명령. 재전송 한도를 넘긴 명령은 TIMEOUT으로 확정하고 목록에서 뺀다.
  std::vector<codec::Command> poll();

  std::vector<CommandEvent> take_events() { return std::exchange(events_, {}); }

 private:
  struct Pending {
    CommandRequest request;
    codec::Command command;
    int sends;
    core::SteadyTime due;  // 다음 판정 시각 (마지막 송신 예정 시각 + 1.0s)
  };
  struct Recent {
    std::optional<CommandRequest> request;  // 없음 = 형식 오류로 거절 (명령 내용을 읽지 못함)
    CommandResult result;
    core::SteadyTime at;
  };

  std::uint32_t issue_id();
  std::vector<Pending>::iterator finish(std::vector<Pending>::iterator it, CmdResult r);
  void remember(std::optional<CommandRequest> request, CommandResult result);
  void repeat(const Recent& r);
  const Pending* pending_of(const std::string& req_id) const;
  const Recent* recent_of(const std::string& req_id);

  const core::IClock& clock_;
  std::uint32_t next_id_;
  std::vector<Pending> pending_;  // 제출 순서. 사건도 이 순서로 나온다
  std::vector<Recent> recent_;    // 결과가 나간 명령, 오래된 것부터 (kRecentResultTtl, kMaxRecentResults)
  std::vector<CommandEvent> events_;
};

}  // namespace ugvc::gateway
