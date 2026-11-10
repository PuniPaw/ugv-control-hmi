#include "ugvc/ingress/validator.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/crc16.hpp"
#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"

namespace ugvc::ingress {
namespace {

using namespace ugvc::codec;

// ⑧ 필드 범위 (ICD 6장). 메시지 타입마다 하나씩, std::visit로 고른다.
bool in_range(const Telemetry& t) {
  return t.lat >= -kLatLimit && t.lat <= kLatLimit && t.lon >= -kLonLimit && t.lon <= kLonLimit &&
         t.heading <= kHeadingMax && t.speed <= kSpeedMax && t.battery <= kBatteryMax &&
         name_of(static_cast<Mode>(t.mode)) != nullptr;
}

bool in_range(const Detection& d) {
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

bool in_range(const Ack& a) { return name_of(static_cast<AckResult>(a.result)) != nullptr; }

// codec 디코더를 Message로 감싼다. 길이가 맞지 않으면(⑦) nullopt.
template <typename T, std::optional<T> (*Decode)(const std::uint8_t*, std::size_t)>
std::optional<Message> decode_as(const std::uint8_t* p, std::size_t n) {
  if (auto m = Decode(p, n)) return Message{std::move(*m)};
  return std::nullopt;
}

// 수신 메시지 규칙표: msg_type마다 보내는 송신원(⑥)과 디코더(⑦ 길이).
// GW가 보내기만 하는 COMMAND·HEARTBEAT는 없다 → 수신하면 ⑥ type으로 폐기.
struct InboundRule {
  std::uint8_t msg_type;
  Source source;
  std::optional<Message> (*decode)(const std::uint8_t*, std::size_t);
};

constexpr std::array<InboundRule, 3> kInboundRules{{
    {kMsgTelemetry, Source::Vehicle, decode_as<Telemetry, decode_telemetry>},
    {kMsgAck, Source::Vehicle, decode_as<Ack, decode_ack>},
    {kMsgDetection, Source::Detector, decode_as<Detection, decode_detection>},
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
  std::optional<Message> message = rule->decode(data + kHeaderSize, h.payload_len);
  if (!message) return RejectReason::Length;                                  // ⑦
  if (!std::visit([](const auto& m) { return in_range(m); }, *message)) {
    return RejectReason::Range;                                               // ⑧
  }
  return CheckedFrame{h, std::move(*message)};
}

}  // namespace ugvc::ingress
