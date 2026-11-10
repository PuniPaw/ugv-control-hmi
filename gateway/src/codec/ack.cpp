#include "ugvc/codec/ack.hpp"

namespace ugvc::codec {

Bytes encode_ack(const Ack& a) {
  Bytes out;
  out.reserve(kAckPayloadSize);
  write_u32_be(out, a.cmd_id);
  write_u8(out, a.result);
  return out;
}

std::optional<Ack> decode_ack(const std::uint8_t* p, std::size_t len) noexcept {
  if (len != kAckPayloadSize) {
    return std::nullopt;
  }
  return Ack{read_u32_be(p), read_u8(p + 4)};
}

}  // namespace ugvc::codec
