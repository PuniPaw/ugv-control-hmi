#include "ugvc/codec/telemetry.hpp"

namespace ugvc::codec {

Bytes encode_telemetry(const Telemetry& t) {
  Bytes out;
  out.reserve(kTelemetryPayloadSize);
  write_i32_be(out, t.lat);
  write_i32_be(out, t.lon);
  write_u16_be(out, t.heading);
  write_u16_be(out, t.speed);
  write_u8(out, t.battery);
  write_u8(out, t.mode);
  write_u16_be(out, t.status_flags);
  return out;
}

std::optional<Telemetry> decode_telemetry(const std::uint8_t* p, std::size_t len) noexcept {
  if (len != kTelemetryPayloadSize) {
    return std::nullopt;
  }
  Telemetry t;
  t.lat = read_i32_be(p + 0);
  t.lon = read_i32_be(p + 4);
  t.heading = read_u16_be(p + 8);
  t.speed = read_u16_be(p + 10);
  t.battery = read_u8(p + 12);
  t.mode = read_u8(p + 13);
  t.status_flags = read_u16_be(p + 14);
  return t;
}

}  // namespace ugvc::codec
