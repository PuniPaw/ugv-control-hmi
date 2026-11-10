#pragma once

#include <chrono>

#include "ugvc/core/clock.hpp"

namespace ugvc::core {

// 주기 작업의 예정 시각 관리 (HEARTBEAT 1Hz, stats 1Hz).
// 시작 즉시 1회, 이후 예정 시각 + period마다. 호출이 늦어져도 몰아서 여러 번 내지 않고 1회만 낸다.
class Periodic {
 public:
  Periodic(const IClock& clock, std::chrono::steady_clock::duration period)
      : clock_(clock), period_(period), next_(clock.now()) {}

  // 예정 시각이 지났으면 true를 돌려주고 다음 예정 시각으로 넘어간다.
  bool due() {
    const SteadyTime now = clock_.now();
    if (now < next_) return false;
    while (next_ <= now) next_ += period_;  // now + period가 아니라 예정 시각 기준: 주기가 밀리지 않는다
    return true;
  }

 private:
  const IClock& clock_;
  std::chrono::steady_clock::duration period_;
  SteadyTime next_;
};

}  // namespace ugvc::core
