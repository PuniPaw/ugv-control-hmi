#pragma once

// HEARTBEAT 송신 (REQ-COM-007): GW가 동작하는 동안 링크 상태와 무관하게 1Hz.

#include <chrono>
#include <cstdint>
#include <optional>

#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/msg_type.hpp"
#include "ugvc/core/clock.hpp"

namespace ugvc::gateway {

class HeartbeatSender {
 public:
  explicit HeartbeatSender(const core::IClock& clock)
      : clock_(clock), period_(clock, std::chrono::seconds(1)) {}

  // 보낼 때가 되었으면 HEARTBEAT 프레임(payload 0B), 아니면 nullopt.
  std::optional<codec::Bytes> poll() {
    if (!period_.due()) return std::nullopt;
    return codec::encode_frame(codec::kMsgHeartbeat, seq_++, clock_.epoch_ms(), nullptr, 0);
  }

 private:
  const core::IClock& clock_;
  core::Periodic period_;
  std::uint32_t seq_ = 0;  // u32 최대값 다음은 0 (부호 없는 정수 순환)
};

}  // namespace ugvc::gateway
