#pragma once

#include <chrono>
#include <cstdint>

#include "ugvc/core/clock.hpp"

namespace ugvc::test {

// 시험용 시계. advance()로만 시간이 흐른다. epoch_ms도 같은 양만큼 증가한다.
class FakeClock final : public core::IClock {
 public:
  explicit FakeClock(std::uint64_t epoch_start_ms = 1'794'729'600'000ULL)
      : epoch_start_ms_(epoch_start_ms) {}

  core::SteadyTime now() const override { return core::SteadyTime{} + elapsed_; }
  std::uint64_t epoch_ms() const override {
    return epoch_start_ms_ + static_cast<std::uint64_t>(
                                 std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_).count());
  }

  void advance(std::chrono::steady_clock::duration d) { elapsed_ += d; }

 private:
  std::uint64_t epoch_start_ms_;
  std::chrono::steady_clock::duration elapsed_{};
};

}  // namespace ugvc::test
