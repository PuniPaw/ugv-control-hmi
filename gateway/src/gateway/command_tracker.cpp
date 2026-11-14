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

std::uint32_t initial_cmd_id(std::uint64_t epoch_ms) {
  const auto id = static_cast<std::uint32_t>((epoch_ms / 100) & 0xFFFF'FFFFULL);
  return id == 0 ? 1 : id;
}

std::uint32_t CommandTracker::issue_id() {
  const auto id = next_id_++;
  if (next_id_ == 0) next_id_ = 1;  // u32 순환 시 0은 건너뛴다
  return id;
}

const CommandTracker::Pending* CommandTracker::pending_of(const std::string& req_id) const {
  const auto it = std::find_if(pending_.begin(), pending_.end(),
                               [&](const Pending& p) { return p.request.req_id == req_id; });
  return it == pending_.end() ? nullptr : &*it;
}

const CommandTracker::Recent* CommandTracker::recent_of(const std::string& req_id) {
  const auto now = clock_.now();
  recent_.erase(std::remove_if(recent_.begin(), recent_.end(),
                               [&](const Recent& r) { return now - r.at >= kRecentResultTtl; }),
                recent_.end());
  const auto it = std::find_if(recent_.begin(), recent_.end(),
                               [&](const Recent& r) { return r.request.req_id == req_id; });
  return it == recent_.end() ? nullptr : &*it;
}

std::optional<codec::Command> CommandTracker::submit(const CommandRequest& req) {
  if (const Pending* p = pending_of(req.req_id)) {
    events_.push_back(CommandIgnored{req.req_id, p->request.same_command(req) ? "req_id pending"
                                                                              : "req_id pending with different command"});
    return std::nullopt;
  }
  if (const Recent* r = recent_of(req.req_id)) {
    if (r->request.same_command(req)) {
      CommandResult again = r->result;
      again.repeated = true;
      events_.push_back(std::move(again));
    } else {
      events_.push_back(CommandIgnored{req.req_id, "req_id used by a recent different command"});
    }
    return std::nullopt;
  }

  const codec::Command cmd{issue_id(), static_cast<std::uint8_t>(req.type), req.lat, req.lon};
  pending_.push_back({req, cmd, 1, clock_.now() + kAckTimeout});
  events_.push_back(CommandSent{req.req_id, cmd.cmd_id, req.type, 1});
  return cmd;
}

void CommandTracker::reject(const std::string& req_id) {
  if (pending_of(req_id) || recent_of(req_id)) {
    events_.push_back(CommandIgnored{req_id, "invalid command with a req_id in use"});
    return;
  }
  events_.push_back(CommandResult{req_id, std::nullopt, CmdResult::RejectedInvalid, 0});
}

void CommandTracker::finish(std::vector<Pending>::iterator it, CmdResult r) {
  CommandResult result{it->request.req_id, it->command.cmd_id, r, it->sends};
  events_.push_back(result);
  recent_.push_back({std::move(it->request), std::move(result), clock_.now()});
  if (recent_.size() > kMaxRecentResults) recent_.erase(recent_.begin());
  pending_.erase(it);
}

void CommandTracker::on_ack(const codec::Ack& ack) {
  const auto it = std::find_if(pending_.begin(), pending_.end(),
                               [&](const Pending& p) { return p.command.cmd_id == ack.cmd_id; });
  const auto result = from_ack(ack.result);
  if (it == pending_.end() || !result) {
    events_.push_back(UnknownAck{ack.cmd_id, ack.result});
    return;
  }
  finish(it, *result);
}

std::vector<codec::Command> CommandTracker::poll() {
  std::vector<codec::Command> resend;
  const auto now = clock_.now();
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (now < it->due) {
      ++it;
    } else if (it->sends < kMaxSends) {
      // 예정 시각 기준으로 다음 판정을 잡는다: 판정 주기(50ms)만큼의 늦음이 송신마다 쌓이지 않게
      ++it->sends;
      it->due += kAckTimeout;
      resend.push_back(it->command);
      events_.push_back(CommandSent{it->request.req_id, it->command.cmd_id,
                                    static_cast<codec::CmdType>(it->command.cmd_type), it->sends});
      ++it;
    } else {
      finish(it, CmdResult::Timeout);
      it = pending_.begin();  // finish가 지운 뒤 처음부터 다시 (순서 유지, 목록은 작다)
    }
  }
  return resend;
}

}  // namespace ugvc::gateway
