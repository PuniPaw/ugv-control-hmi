#pragma once

// 시계 주입 (STP 2.2). 시간에 따라 동작이 바뀌는 모듈은 IClock을 생성자로 받는다.
// - now(): 경과 시간 판정용 단조 시계. 시스템 시각이 NTP로 뒤로 가도 링크 판정이 흔들리지 않는다.
// - epoch_ms(): 패킷 timestamp, rx_ts, 기록용 절대 시각.

#include <chrono>
#include <cstdint>

namespace ugvc::core {

using SteadyTime = std::chrono::steady_clock::time_point;
using Millis = std::chrono::milliseconds;

class IClock {
 public:
  virtual ~IClock() = default;
  virtual SteadyTime now() const = 0;
  virtual std::uint64_t epoch_ms() const = 0;
};

class SystemClock final : public IClock {
 public:
  SteadyTime now() const override { return std::chrono::steady_clock::now(); }
  std::uint64_t epoch_ms() const override {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
  }
};
}  // namespace ugvc::core
