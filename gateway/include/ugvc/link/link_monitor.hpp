#pragma once

// 송신원별 링크 상태 판정 (REQ-COM-003, REQ-COM-004, STP 5장)
//
// 상태 = 마지막 유효 패킷 후 경과 시간의 함수:  < 0.5s CONNECTED,  ≥ 0.5s DEGRADED,  ≥ 1.0s LOST.
// 첫 유효 패킷 전은 LOST. 상태가 바뀔 때만 전이(LinkChange)를 반환한다 (시작 시 초기값은 전이 아님).
// 콜백 대신 반환값: 호출자(Gateway)가 송신원을 붙여 모으고, 실행 파일이 꺼내서 출력한다.

#include <chrono>
#include <cstdint>
#include <optional>

#include "ugvc/core/clock.hpp"

namespace ugvc::link {

enum class LinkState : std::uint8_t { Connected, Degraded, Lost };

constexpr const char* to_string(LinkState s) {
  switch (s) {
    case LinkState::Connected: return "CONNECTED";
    case LinkState::Degraded: return "DEGRADED";
    case LinkState::Lost: return "LOST";
  }
  return "LOST";
}

inline constexpr std::chrono::milliseconds kDegradedAfter{500};
inline constexpr std::chrono::milliseconds kLostAfter{1000};

struct LinkChange {
  LinkState from;
  LinkState to;
  std::uint64_t at_epoch_ms;
  std::optional<std::uint64_t> last_rx_epoch_ms;  // 첫 패킷 전이면 nullopt
};

class LinkMonitor {
 public:
  explicit LinkMonitor(const core::IClock& clock) : clock_(clock) {}

  // 유효 패킷 수신: 경과 시간을 0으로 되돌린다. LOST에서도 DEGRADED를 거치지 않고 바로 CONNECTED.
  // 상태가 바뀌었으면 그 전이를 반환한다. [[nodiscard]]: 반환값을 버리면 컴파일 경고 → 전이를 잃지 않게
  [[nodiscard]] std::optional<LinkChange> on_valid_packet() {
    last_rx_ = clock_.now();
    last_rx_epoch_ms_ = clock_.epoch_ms();
    return set(LinkState::Connected);
  }

  // 현재 시각으로 상태를 다시 판정한다. GW는 판정 주기(50ms)마다, 그리고 패킷 처리 직전에 부른다.
  [[nodiscard]] std::optional<LinkChange> poll() {
    if (!last_rx_) return std::nullopt;  // 첫 패킷 전: LOST 유지
    const auto elapsed = clock_.now() - *last_rx_;
    return set(elapsed >= kLostAfter       ? LinkState::Lost
               : elapsed >= kDegradedAfter ? LinkState::Degraded
                                           : LinkState::Connected);
  }

  LinkState state() const { return state_; }
  std::optional<std::uint64_t> last_rx_epoch_ms() const { return last_rx_epoch_ms_; }

 private:
  std::optional<LinkChange> set(LinkState next) {
    if (next == state_) return std::nullopt;
    const LinkChange c{state_, next, clock_.epoch_ms(), last_rx_epoch_ms_};
    state_ = next;
    return c;
  }

  const core::IClock& clock_;
  LinkState state_ = LinkState::Lost;
  std::optional<core::SteadyTime> last_rx_;
  std::optional<std::uint64_t> last_rx_epoch_ms_;
};

}  // namespace ugvc::link
