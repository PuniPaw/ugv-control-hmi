#pragma once

// Big-endian 바이트 읽기·쓰기 헬퍼 (ADR-002)
//
// 구조체 memcpy·#pragma pack 대신 필드를 하나씩 시프트로 쓰고 읽는다.
// 이렇게 하면 CPU 엔디언·컴파일러 패딩과 무관하게 ICD 표와 바이트가 1:1로 대응한다.
//
// read_* 함수는 길이를 검사하지 않는다. 호출 측(검증 규칙 ①④⑦)이 먼저 길이를 확인한다.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ugvc::codec {

using Bytes = std::vector<std::uint8_t>;

inline void write_u8(Bytes& out, std::uint8_t v) { out.push_back(v); }

inline void write_u16_be(Bytes& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v >> 8));
  out.push_back(static_cast<std::uint8_t>(v));
}

inline void write_u32_be(Bytes& out, std::uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>(v >> shift));
  }
}

inline void write_u64_be(Bytes& out, std::uint64_t v) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>(v >> shift));
  }
}

// 부호 있는 정수는 2의 보수 비트 패턴을 그대로 보낸다 (C++20부터 표준 보장, C++17 주요 컴파일러 동일)
inline void write_i32_be(Bytes& out, std::int32_t v) {
  write_u32_be(out, static_cast<std::uint32_t>(v));
}

inline std::uint8_t read_u8(const std::uint8_t* p) { return p[0]; }

inline std::uint16_t read_u16_be(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}

inline std::uint32_t read_u32_be(const std::uint8_t* p) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    v = (v << 8) | p[i];
  }
  return v;
}

inline std::uint64_t read_u64_be(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | p[i];
  }
  return v;
}

inline std::int32_t read_i32_be(const std::uint8_t* p) {
  return static_cast<std::int32_t>(read_u32_be(p));
}

}  // namespace ugvc::codec
