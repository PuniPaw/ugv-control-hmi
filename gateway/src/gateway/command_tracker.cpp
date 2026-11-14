#include "ugvc/gateway/command_tracker.hpp"

#include <algorithm>

namespace ugvc::gateway {
namespace {

constexpr int kMaxSends = 1 + kMaxRetransmits;

// 표 6-5 값이 아니면 nullopt (검증 ⑧이 걸러 주지만 이 모듈도 전체 입력에 대해 정의해 둔다)
std::optional<CmdResult> from_ack(std::uint8_t r) {
  switch (static_cast<codec::AckResult>(r)) {
    case codec::AckResult::Accepted: return CmdResult::Accepted;
    case codec::AckResult::RejectedInvalid: return CmdResult::RejectedInvalid;
    case codec::AckResult::RejectedState: return CmdResult::RejectedState;
  }
  return std::nullopt;
}

}  // namespace

std::uint32_t CommandTracker::issue_id() {
  const auto id = next_id_++;
  if (next_id_ == 0) next_id_ = 1;  // u32 순환 시 0은 건너뛴다
  return id;
}

std::optional<codec::Command> CommandTracker::submit(const if03::CommandRequest& req) {
  const bool duplicate = std::any_of(pending_.begin(), pending_.end(),
                                     [&](const Pending& p) { return p.req_id == req.req_id; });
  if (duplicate) return std::nullopt;

  const codec::Command cmd{issue_id(), static_cast<std::uint8_t>(req.type), req.lat, req.lon};
  pending_.push_back({req.req_id, cmd, 1, clock_.now()});
  events_.push_back(CommandSent{req.req_id, cmd.cmd_id, req.type, 1});
  return cmd;
}

void CommandTracker::on_ack(const codec::Ack& ack) {
  const auto it = std::find_if(pending_.begin(), pending_.end(),
                               [&](const Pending& p) { return p.command.cmd_id == ack.cmd_id; });
  const auto result = from_ack(ack.result);
  if (it == pending_.end() || !result) {
    events_.push_back(UnknownAck{ack.cmd_id, ack.result});
    return;
  }
  events_.push_back(CommandResult{it->req_id, it->command.cmd_id, *result, it->sends});
  pending_.erase(it);
}

std::vector<codec::Command> CommandTracker::poll() {
  std::vector<codec::Command> resend;
  const auto now = clock_.now();
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (now - it->last_send < kAckTimeout) {
      ++it;
    } else if (it->sends < kMaxSends) {
      ++it->sends;
      it->last_send = now;
      resend.push_back(it->command);
      events_.push_back(CommandSent{it->req_id, it->command.cmd_id,
                                    static_cast<codec::CmdType>(it->command.cmd_type), it->sends});
      ++it;
    } else {
      events_.push_back(CommandResult{it->req_id, it->command.cmd_id, CmdResult::Timeout, it->sends});
      it = pending_.erase(it);
    }
  }
  return resend;
}

}  // namespace ugvc::gateway
