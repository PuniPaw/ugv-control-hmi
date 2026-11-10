#include "ugvc/codec/detection.hpp"

namespace ugvc::codec {

Bytes encode_detection(const Detection& d) {
  Bytes out;
  out.reserve(detection_payload_size(d.objects.size()));
  write_u32_be(out, d.frame_id);
  write_u64_be(out, d.capture_ts);
  write_u16_be(out, d.img_w);
  write_u16_be(out, d.img_h);
  write_u8(out, static_cast<std::uint8_t>(d.objects.size()));
  for (const auto& o : d.objects) {
    write_u8(out, o.class_id);
    write_u16_be(out, o.confidence);
    write_u16_be(out, o.x);
    write_u16_be(out, o.y);
    write_u16_be(out, o.w);
    write_u16_be(out, o.h);
  }
  return out;
}

std::optional<Detection> decode_detection(const std::uint8_t* p, std::size_t len) {
  if (len < kDetectionHeadSize) {
    return std::nullopt;
  }
  const std::uint8_t count = read_u8(p + 16);
  if (len != detection_payload_size(count)) {
    return std::nullopt;
  }
  Detection d;
  d.frame_id = read_u32_be(p + 0);
  d.capture_ts = read_u64_be(p + 4);
  d.img_w = read_u16_be(p + 12);
  d.img_h = read_u16_be(p + 14);
  d.objects.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t* o = p + kDetectionHeadSize + i * kDetectedObjectSize;
    d.objects.push_back({read_u8(o + 0), read_u16_be(o + 1), read_u16_be(o + 3),
                         read_u16_be(o + 5), read_u16_be(o + 7), read_u16_be(o + 9)});
  }
  return d;
}

}  // namespace ugvc::codec
