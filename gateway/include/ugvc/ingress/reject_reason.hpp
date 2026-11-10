#pragma once

// 폐기 원인과 원인별 오류 계수 (ICD 9장, IF-03 stats.errors)

#include <array>
#include <numeric>
#include <cstddef>
#include <cstdint>

namespace ugvc::ingress {

// 값 순서 = IF-03 stats.errors 키 순서. 이름은 to_string()과 ICD 9장 "오류 계수" 열이 정본.
enum class RejectReason : std::uint8_t {
  Sync,
  Version,
  Length,
  Crc,
  Type,
  Range,
  Order,
};

inline constexpr std::size_t kRejectReasonCount = 7;

constexpr const char* to_string(RejectReason r) {
  constexpr const char* kNames[kRejectReasonCount] = {"sync",  "version", "length", "crc",
                                                       "type",  "range",   "order"};
  return kNames[static_cast<std::size_t>(r)];
}

class ErrorCounters {
 public:
  void add(RejectReason r) { ++counts_[static_cast<std::size_t>(r)]; }
  std::uint64_t operator[](RejectReason r) const { return counts_[static_cast<std::size_t>(r)]; }

  std::uint64_t total() const {
    return std::accumulate(counts_.begin(), counts_.end(), std::uint64_t{0});
  }

 private:
  std::array<std::uint64_t, kRejectReasonCount> counts_{};
};

}  // namespace ugvc::ingress
