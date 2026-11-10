#pragma once

// DETECTION (msg_type 0x02, payload 17 + 11 × count) — ICD 6.2

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ugvc/codec/bytes.hpp"

namespace ugvc::codec {

inline constexpr std::uint8_t kMsgDetection = 0x02;
inline constexpr std::size_t kDetectionHeadSize = 17;
inline constexpr std::size_t kDetectedObjectSize = 11;

// 표 6-3 class_id (TBD-02: 부트캠프 모델 확정 시 갱신)
enum class ClassId : std::uint8_t {
  Person = 0,
  Vehicle = 1,
  ArmoredVehicle = 2,
  Unknown = 255,
};

struct DetectedObject {
  std::uint8_t class_id = 0;
  std::uint16_t confidence = 0;  // 0.0001
  std::uint16_t x = 0;           // px, 박스 좌상단
  std::uint16_t y = 0;
  std::uint16_t w = 0;
  std::uint16_t h = 0;

  bool operator==(const DetectedObject& o) const {
    return class_id == o.class_id && confidence == o.confidence && x == o.x && y == o.y &&
           w == o.w && h == o.h;
  }
};

struct Detection {
  std::uint32_t frame_id = 0;
  std::uint64_t capture_ts = 0;  // ms
  std::uint16_t img_w = 0;
  std::uint16_t img_h = 0;
  std::vector<DetectedObject> objects;  // count = objects.size()

  bool operator==(const Detection& o) const {
    return frame_id == o.frame_id && capture_ts == o.capture_ts && img_w == o.img_w &&
           img_h == o.img_h && objects == o.objects;
  }
};

constexpr std::size_t detection_payload_size(std::size_t count) {
  return kDetectionHeadSize + kDetectedObjectSize * count;
}

Bytes encode_detection(const Detection& d);

// payload 길이가 17 + 11 × count와 맞지 않으면 nullopt. 값 범위는 검사하지 않는다.
std::optional<Detection> decode_detection(const std::uint8_t* payload, std::size_t len);

}  // namespace ugvc::codec
