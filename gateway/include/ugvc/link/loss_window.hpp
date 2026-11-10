#pragma once

// 최근 10초 유실률 (REQ-COM-005, STP 5장): 유실 / (수신 + 유실), 1초 버킷 10개, 비면 값 없음.
//
// 버킷은 steady 시계의 "초 번호"로 식별한다. 번호 % 10 자리에 쓰고, 자리에 남은 번호가 다르면
// 오래된 버킷이므로 비우고 쓴다. 읽을 때는 현재 초와 이전 9초에 해당하는 버킷만 더한다.

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

#include "ugvc/core/clock.hpp"

namespace ugvc::link {

class LossWindow {
 public:
  static constexpr int kSeconds = 10;

  void record(core::SteadyTime at, std::uint64_t received, std::uint64_t lost) {
    const std::int64_t sec = second_of(at);
    Bucket& b = buckets_[slot(sec)];
    if (b.second != sec) b = Bucket{sec, 0, 0};
    b.received += received;
    b.lost += lost;
  }

  std::optional<double> rate(core::SteadyTime now) const {
    const std::int64_t current = second_of(now);
    std::uint64_t received = 0;
    std::uint64_t lost = 0;
    for (const auto& b : buckets_) {
      const std::int64_t age = current - b.second;
      if (age >= 0 && age < kSeconds) {
        received += b.received;
        lost += b.lost;
      }
    }
    if (received + lost == 0) return std::nullopt;
    return static_cast<double>(lost) / static_cast<double>(received + lost);
  }

 private:
  static constexpr std::int64_t kEmpty = std::numeric_limits<std::int64_t>::min() / 2;  // 어떤 현재 초와도 10초 안에 들지 않는 값

  struct Bucket {
    std::int64_t second = kEmpty;
    std::uint64_t received = 0;
    std::uint64_t lost = 0;
  };

  static std::int64_t second_of(core::SteadyTime t) {
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
  }
  static std::size_t slot(std::int64_t sec) {
    return static_cast<std::size_t>(((sec % kSeconds) + kSeconds) % kSeconds);
  }

  std::array<Bucket, kSeconds> buckets_{};
};

}  // namespace ugvc::link
