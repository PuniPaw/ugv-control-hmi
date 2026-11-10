#pragma once

// 검증 규칙 ⑨ seq 순서와 유실 수 (ICD 9장, STP 5장). 스트림(송신원, msg_type)마다 하나.

#include <cstdint>
#include <optional>

namespace ugvc::ingress {

// RFC 1982 순환 비교: a가 b보다 "큰"(뒤의) seq인가. 차이 0은 중복, 정확히 2^31은 "크지 않음".
constexpr bool seq_after(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t diff = a - b;  // u32 뺄셈은 2^32로 나눈 나머지 (부호 없는 정수 순환)
  return diff != 0 && diff < 0x8000'0000U;
}

class SeqTracker {
 public:
  struct Result {
    bool in_order;      // false면 규칙 ⑨ 위반: 역순 도착(중복 포함) → order로 폐기
    std::uint32_t lost; // in_order일 때 건너뛴 seq 개수
  };

  Result observe(std::uint32_t seq) {
    if (last_ && !seq_after(seq, *last_)) {
      return {false, 0};  // 늦게 도착해도 이미 센 유실은 되돌리지 않는다
    }
    const std::uint32_t lost = last_ ? seq - *last_ - 1U : 0U;
    last_ = seq;
    return {true, lost};
  }

  // 다음 패킷을 비교 없이 기준값으로 삼는다 (LOST 후 첫 유효 패킷: 송신원 재시작 대비)
  void rebase() { last_.reset(); }

 private:
  std::optional<std::uint32_t> last_;  // nullopt: 스트림의 첫 패킷 전
};

}  // namespace ugvc::ingress
