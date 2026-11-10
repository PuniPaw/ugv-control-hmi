#pragma once

// 송신원별 링크 상태 판정 (REQ-COM-003, REQ-COM-004, STP 5장)
//
// 상태 = 마지막 유효 패킷 후 경과 시간의 함수:  < 0.5s CONNECTED,  ≥ 0.5s DEGRADED,  ≥ 1.0s LOST.
// 첫 유효 패킷 전은 LOST. 상태가 바뀔 때만 ILinkTransitionSink로 전이를 알린다 (시작 시 초기값은 전이 아님).

#include <chrono>
#include <cstdint>
#include <optional>

#include "ugvc/core/clock.hpp"
#include "ugvc/ingress/source.hpp"

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

struct LinkTransition {
  ingress::Source source;
  LinkState from;
  LinkState to;
  std::uint64_t at_epoch_ms;
  std::optional<std::uint64_t> last_rx_epoch_ms;  // 첫 패킷 전이면 nullopt
};

class ILinkTransitionSink {
 public:
  virtual ~ILinkTransitionSink() = default;
  virtual void on_link_transition(const LinkTransition& t) = 0;
};

class LinkMonitor {
 public:
  LinkMonitor(ingress::Source source, const core::IClock& clock, ILinkTransitionSink& sink)
      : source_(source), clock_(clock), sink_(sink) {}

  // 유효 패킷 수신: 경과 시간을 0으로 되돌린다. LOST에서도 DEGRADED를 거치지 않고 바로 CONNECTED.
  void on_valid_packet() {
    last_rx_ = clock_.now();
    last_rx_epoch_ms_ = clock_.epoch_ms();
    set(LinkState::Connected);
  }

  // 현재 시각으로 상태를 다시 판정한다. GW는 판정 주기(50ms)마다, 그리고 패킷 처리 직전에 부른다.
  void poll() {
    if (!last_rx_) return;  // 첫 패킷 전: LOST 유지
    const auto elapsed = clock_.now() - *last_rx_;
    set(elapsed >= kLostAfter       ? LinkState::Lost
        : elapsed >= kDegradedAfter ? LinkState::Degraded
                                    : LinkState::Connected);
  }

  LinkState state() const { return state_; }
  std::optional<std::uint64_t> last_rx_epoch_ms() const { return last_rx_epoch_ms_; }

 private:
  void set(LinkState next) {
    if (next == state_) return;
    const LinkTransition t{source_, state_, next, clock_.epoch_ms(), last_rx_epoch_ms_};
    state_ = next;
    sink_.on_link_transition(t);
  }

  ingress::Source source_;
  const core::IClock& clock_;
  ILinkTransitionSink& sink_;
  LinkState state_ = LinkState::Lost;
  std::optional<core::SteadyTime> last_rx_;
  std::optional<std::uint64_t> last_rx_epoch_ms_;
};

}  // namespace ugvc::link
