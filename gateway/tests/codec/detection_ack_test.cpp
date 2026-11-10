#include <gtest/gtest.h>

#include "support/vectors.hpp"
#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/detection.hpp"

using namespace ugvc::codec;
using ugvc::test::from_hex;
using ugvc::test::load_vectors;

namespace {

Detection detection_of(const nlohmann::json& f) {
  Detection d;
  d.frame_id = f.at("frame_id").get<std::uint32_t>();
  d.capture_ts = f.at("capture_ts").get<std::uint64_t>();
  d.img_w = f.at("img_w").get<std::uint16_t>();
  d.img_h = f.at("img_h").get<std::uint16_t>();
  for (const auto& o : f.at("objects")) {
    d.objects.push_back({o.at("class_id").get<std::uint8_t>(), o.at("confidence").get<std::uint16_t>(),
                         o.at("x").get<std::uint16_t>(), o.at("y").get<std::uint16_t>(),
                         o.at("w").get<std::uint16_t>(), o.at("h").get<std::uint16_t>()});
  }
  return d;
}

}  // namespace

// ADR-002: DETECTION 인코딩·디코딩이 벡터와 바이트 단위로 일치 (count 0, 32 포함)
TEST(Codec, REQ_QUAL_001_detection_matches_vectors) {
  for (const auto& v : load_vectors("detection.json").at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const Detection expected = detection_of(v.at("fields"));
    const auto payload = from_hex(v.at("payload_hex"));
    EXPECT_EQ(encode_detection(expected), payload);
    const auto decoded = decode_detection(payload.data(), payload.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, expected);
  }
}

TEST(Codec, REQ_COM_002_detection_length_must_match_count) {
  Detection d;
  d.img_w = 640;
  d.img_h = 480;
  d.objects.push_back({0, 1, 0, 0, 1, 1});
  auto payload = encode_detection(d);
  payload.pop_back();
  EXPECT_FALSE(decode_detection(payload.data(), payload.size()).has_value());
  const Bytes sixteen(16, 0x00);
  EXPECT_FALSE(decode_detection(sixteen.data(), sixteen.size()).has_value());
}

TEST(Codec, REQ_QUAL_001_ack_matches_vectors) {
  for (const auto& v : load_vectors("ack.json").at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const Ack expected{v.at("fields").at("cmd_id").get<std::uint32_t>(),
                       v.at("fields").at("result").get<std::uint8_t>()};
    const auto payload = from_hex(v.at("payload_hex"));
    EXPECT_EQ(encode_ack(expected), payload);
    EXPECT_EQ(decode_ack(payload.data(), payload.size()), expected);
  }
}
