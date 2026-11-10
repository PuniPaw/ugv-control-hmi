#include "ugvc/ingress/validator.hpp"

#include <algorithm>
#include <array>

#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/crc16.hpp"
#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"

namespace ugvc::ingress {
namespace {

using namespace ugvc::codec;

// ⑦ payload 길이. ⑧ 범위는 ⑦을 통과한 payload에만 호출된다.
bool telemetry_length_ok(const std::uint8_t*, std::size_t n) { return n == kTelemetryPayloadSize; }

bool telemetry_range_ok(const std::uint8_t* p, std::size_t n) {
  const Telemetry t = *decode_telemetry(p, n);
  return t.lat >= -kLatLimit && t.lat <= kLatLimit && t.lon >= -kLonLimit && t.lon <= kLonLimit &&
         t.heading <= kHeadingMax && t.speed <= kSpeedMax && t.battery <= kBatteryMax &&
         name_of(static_cast<Mode>(t.mode)) != nullptr;
}

bool detection_length_ok(const std::uint8_t* p, std::size_t n) {
  return n >= kDetectionHeadSize && n == detection_payload_size(read_u8(p + 16));
}

bool detection_range_ok(const std::uint8_t* p, std::size_t n) {
  const Detection d = *decode_detection(p, n);
  if (d.img_w < 1 || d.img_w > kImageSizeMax || d.img_h < 1 || d.img_h > kImageSizeMax ||
      d.objects.size() > kMaxObjects) {
    return false;
  }
  return std::all_of(d.objects.begin(), d.objects.end(), [&](const DetectedObject& o) {
    // 박스는 이미지 안: x ≤ img_w-1, 1 ≤ w ≤ img_w-x (y, h도 같음). 뺄셈은 x < img_w 확인 후.
    return name_of(static_cast<ClassId>(o.class_id)) != nullptr && o.confidence <= kConfidenceMax &&
           o.x < d.img_w && o.w >= 1 && o.w <= d.img_w - o.x &&       //
           o.y < d.img_h && o.h >= 1 && o.h <= d.img_h - o.y;
  });
}

bool ack_length_ok(const std::uint8_t*, std::size_t n) { return n == kAckPayloadSize; }

bool ack_range_ok(const std::uint8_t* p, std::size_t n) {
  return name_of(static_cast<AckResult>(decode_ack(p, n)->result)) != nullptr;
}

// 수신 메시지 규칙표: msg_type마다 보내는 송신원(⑥), 길이(⑦), 범위(⑧).
// GW가 보내기만 하는 COMMAND·HEARTBEAT는 없다 → 수신하면 ⑥ type으로 폐기.
struct InboundRule {
  std::uint8_t msg_type;
  Source source;
  bool (*length_ok)(const std::uint8_t*, std::size_t);
  bool (*range_ok)(const std::uint8_t*, std::size_t);
};

constexpr std::array<InboundRule, 3> kInboundRules{{
    {kMsgTelemetry, Source::Vehicle, telemetry_length_ok, telemetry_range_ok},
    {kMsgAck, Source::Vehicle, ack_length_ok, ack_range_ok},
    {kMsgDetection, Source::Detector, detection_length_ok, detection_range_ok},
}};

const InboundRule* find_rule(Source source, std::uint8_t msg_type) {
  const auto it = std::find_if(kInboundRules.begin(), kInboundRules.end(), [&](const InboundRule& r) {
    return r.msg_type == msg_type && r.source == source;
  });
  return it == kInboundRules.end() ? nullptr : &*it;
}

}  // namespace

CheckResult check_frame(Source source, const std::uint8_t* data, std::size_t len) {
  if (len < kFrameOverhead || len > kMaxFrameSize) return RejectReason::Length;  // ①
  const Header h = *decode_header(data, len);
  if (h.sync != kSync) return RejectReason::Sync;                             // ②
  if (h.version != kVersion) return RejectReason::Version;                    // ③
  if (len != kFrameOverhead + h.payload_len) return RejectReason::Length;     // ④
  if (*read_frame_crc(data, len) != crc16_ccitt_false(data, len - kCrcSize)) {
    return RejectReason::Crc;                                                 // ⑤
  }
  const InboundRule* rule = find_rule(source, h.msg_type);
  if (rule == nullptr) return RejectReason::Type;                             // ⑥
  const std::uint8_t* payload = data + kHeaderSize;
  if (!rule->length_ok(payload, h.payload_len)) return RejectReason::Length;  // ⑦
  if (!rule->range_ok(payload, h.payload_len)) return RejectReason::Range;    // ⑧
  return CheckedFrame{h, payload};
}

}  // namespace ugvc::ingress
