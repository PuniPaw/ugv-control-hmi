#pragma once

// 폐기 원인과 원인별 오류 계수 (ICD 9장, IF-03 stats.errors)

#include <array>
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

inline constexpr std::array<RejectReason, 7> kAllRejectReasons{
    RejectReason::Sync, RejectReason::Version, RejectReason::Length, RejectReason::Crc,
    RejectReason::Type, RejectReason::Range,   RejectReason::Order};
inline constexpr std::size_t kRejectReasonCount = kAllRejectReasons.size();

// switch: 값이 추가되면 -Wswitch(-Werror)가 이름 누락을 알린다 (병렬 배열은 순서가 어긋나도 조용하다)
constexpr const char* to_string(RejectReason r) {
  switch (r) {
    case RejectReason::Sync: return "sync";
    case RejectReason::Version: return "version";
    case RejectReason::Length: return "length";
    case RejectReason::Crc: return "crc";
    case RejectReason::Type: return "type";
    case RejectReason::Range: return "range";
    case RejectReason::Order: return "order";
  }
  return "unknown";
}

// kAllRejectReasons가 열거값을 모두, 순서대로 담고 있는지 컴파일 시간에 확인한다 (ErrorCounters가 값을 색인으로 쓴다)
constexpr bool all_reasons_in_order() {
  for (std::size_t i = 0; i < kRejectReasonCount; ++i) {
    if (static_cast<std::size_t>(kAllRejectReasons[i]) != i) return false;
  }
  return static_cast<std::size_t>(RejectReason::Order) + 1 == kRejectReasonCount;
}
static_assert(all_reasons_in_order(), "kAllRejectReasons must list every RejectReason in enum order");

class ErrorCounters {
 public:
  void add(RejectReason r) { ++counts_[static_cast<std::size_t>(r)]; }
  std::uint64_t operator[](RejectReason r) const { return counts_[static_cast<std::size_t>(r)]; }

 private:
  std::array<std::uint64_t, kRejectReasonCount> counts_{};
};

}  // namespace ugvc::ingress
