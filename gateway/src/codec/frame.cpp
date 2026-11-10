#include "ugvc/codec/frame.hpp"

#include "ugvc/codec/crc16.hpp"

namespace ugvc::codec {

Bytes encode_frame(std::uint8_t msg_type, std::uint32_t seq, std::uint64_t timestamp_ms,
                   const std::uint8_t* payload, std::size_t payload_len) {
  Bytes out;
  out.reserve(kFrameOverhead + payload_len);  // 재할당 없이 한 번에 확보

  write_u16_be(out, kSync);
  write_u8(out, kVersion);
  write_u8(out, msg_type);
  write_u32_be(out, seq);
  write_u64_be(out, timestamp_ms);
  write_u16_be(out, static_cast<std::uint16_t>(payload_len));
  out.insert(out.end(), payload, payload + payload_len);

  // CRC 범위: 헤더 시작 ~ payload 끝 (ICD 3장)
  write_u16_be(out, crc16_ccitt_false(out.data(), out.size()));
  return out;
}

std::optional<Header> decode_header(const std::uint8_t* data, std::size_t len) noexcept {
  if (len < kHeaderSize) {
    return std::nullopt;
  }
  Header h;
  h.sync = read_u16_be(data + 0);
  h.version = read_u8(data + 2);
  h.msg_type = read_u8(data + 3);
  h.seq = read_u32_be(data + 4);
  h.timestamp_ms = read_u64_be(data + 8);
  h.payload_len = read_u16_be(data + 16);
  return h;
}

std::optional<std::uint16_t> read_frame_crc(const std::uint8_t* data, std::size_t len) noexcept {
  if (len < kFrameOverhead) {
    return std::nullopt;
  }
  return read_u16_be(data + len - kCrcSize);
}

}  // namespace ugvc::codec
