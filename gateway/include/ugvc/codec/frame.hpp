#pragma once

// 공통 헤더(18B) + payload(N) + CRC16(2B) 프레임 (ICD 4장)

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ugvc/codec/bytes.hpp"

namespace ugvc::codec {

inline constexpr std::uint16_t kSync = 0xAA55;
inline constexpr std::uint8_t kVersion = 0x01;
inline constexpr std::size_t kHeaderSize = 18;
inline constexpr std::size_t kCrcSize = 2;
inline constexpr std::size_t kFrameOverhead = kHeaderSize + kCrcSize;  // 20
inline constexpr std::size_t kMaxFrameSize = 1200;

// 헤더 필드를 받은 그대로 담는다. 값이 올바른지(sync, version 등)는 검증 단계가 판단한다.
struct Header {
  std::uint16_t sync = 0;
  std::uint8_t version = 0;
  std::uint8_t msg_type = 0;
  std::uint32_t seq = 0;
  std::uint64_t timestamp_ms = 0;
  std::uint16_t payload_len = 0;

  bool operator==(const Header& o) const {
    return sync == o.sync && version == o.version && msg_type == o.msg_type && seq == o.seq &&
           timestamp_ms == o.timestamp_ms && payload_len == o.payload_len;
  }
};

// 헤더 + payload + CRC를 붙인 완성 프레임을 만든다. sync·version은 ICD 고정값을 쓴다.
// payload_len이 u16 범위나 최대 프레임 크기를 넘는지는 호출 측 책임이다.
Bytes encode_frame(std::uint8_t msg_type, std::uint32_t seq, std::uint64_t timestamp_ms,
                   const std::uint8_t* payload, std::size_t payload_len);

// 앞 18바이트를 헤더로 읽는다. len < 18이면 nullopt.
std::optional<Header> decode_header(const std::uint8_t* data, std::size_t len) noexcept;

// 프레임 끝 2바이트의 CRC 필드 값을 읽는다. len < 20이면 nullopt.
std::optional<std::uint16_t> read_frame_crc(const std::uint8_t* data, std::size_t len) noexcept;

}  // namespace ugvc::codec
