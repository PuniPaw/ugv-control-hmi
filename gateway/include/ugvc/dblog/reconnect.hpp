#pragma once

// DB 재접속 시각 (REQ-LOG-003). 접속에 실패할 때마다 간격을 두 배로 늘리되 상한에서 멈추고,
// succeeded()를 부르면 처음 간격으로 되돌린다. 포기하지 않는다: GW가 도는 동안 계속 시도한다.
//
// 시계를 주입받아(STP 2.2) 단위 시험은 FakeClock으로 간격 경계를 본다.
// DB 쓰기 스레드는 due_at()까지 condition_variable로 기다린다.

#include <algorithm>
#include <chrono>

#include "ugvc/core/clock.hpp"

namespace ugvc::dblog {

inline constexpr std::chrono::milliseconds kReconnectFirst{500};
inline constexpr std::chrono::milliseconds kReconnectMax{5000};

class ReconnectSchedule {
 public:
  explicit ReconnectSchedule(const core::IClock& clock, std::chrono::milliseconds first = kReconnectFirst,
                             std::chrono::milliseconds max = kReconnectMax)
      : clock_(clock), first_(first), max_(max), next_delay_(first), due_(clock.now()) {}

  // 지금 접속을 시도할 때인가. 처음에는 바로 시도한다.
  bool due() const { return clock_.now() >= due_; }
  core::SteadyTime due_at() const { return due_; }

  // 시도가 실패했다: 다음 시도는 지금부터 현재 간격 뒤. 간격은 두 배로 (상한 max).
  // 반환값은 이번에 잡은 간격 (진단 기록용).
  std::chrono::milliseconds failed() {
    const auto delay = next_delay_;
    due_ = clock_.now() + delay;
    next_delay_ = std::min(next_delay_ * 2, max_);
    return delay;
  }

  // 성공했다고 확인됐다(DbSink는 접속 뒤 첫 저장 성공 때 부른다. 접속 성공만으로는 부르지 않으므로,
  // 접속 실패로 늘어난 간격은 그 연결로 저장할 때까지 이어진다). 다음 실패부터 다시 처음 간격으로 센다.
  void succeeded() {
    next_delay_ = first_;
    due_ = clock_.now();
  }

 private:
  const core::IClock& clock_;
  std::chrono::milliseconds first_;
  std::chrono::milliseconds max_;
  std::chrono::milliseconds next_delay_;
  core::SteadyTime due_;
};

}  // namespace ugvc::dblog
