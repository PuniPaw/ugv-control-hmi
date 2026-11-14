#include "ugvc/codec/command.hpp"

namespace ugvc::codec {

Bytes encode_command(const Command& c) {
  Bytes out;
  out.reserve(kCommandPayloadSize);
  write_u32_be(out, c.cmd_id);
  write_u8(out, c.cmd_type);
  write_i32_be(out, c.lat);
  write_i32_be(out, c.lon);
  return out;
}

}  // namespace ugvc::codec
