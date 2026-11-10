#include "ugvc/codec/telemetry.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include "support/vectors.hpp"

using namespace ugvc::codec;
using ugvc::test::from_hex;
using ugvc::test::load_vectors;

namespace {

const nlohmann::json& telemetry_vectors() {
  static const nlohmann::json doc = load_vectors("telemetry.json");
  return doc;
}

nlohmann::json find(const std::string& name) {  // 값 반환: 시험용이라 복사 비용 무시
  const auto& vs = telemetry_vectors().at("vectors");
  const auto it = std::find_if(vs.begin(), vs.end(), [&](const nlohmann::json& v) { return v.at("name") == name; });
  if (it == vs.end()) throw std::runtime_error("no telemetry vector: " + name);
  return *it;
}

Telemetry fields_of(const nlohmann::json& v) {
  const auto& f = v.at("fields");
  Telemetry t;
  t.lat = f.at("lat").get<std::int32_t>();
  t.lon = f.at("lon").get<std::int32_t>();
  t.heading = f.at("heading").get<std::uint16_t>();
  t.speed = f.at("speed").get<std::uint16_t>();
  t.battery = f.at("battery").get<std::uint8_t>();
  t.mode = f.at("mode").get<std::uint8_t>();
  t.status_flags = f.at("status_flags").get<std::uint16_t>();
  return t;
}

void expect_decodes_to_fields(const std::string& name) {
  SCOPED_TRACE(name);
  const auto v = find(name);
  const auto payload = from_hex(v.at("payload_hex"));
  const auto t = decode_telemetry(payload.data(), payload.size());
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(*t, fields_of(v));
}

}  // namespace

// TC-COM-001-01: 정상 TELEMETRY 디코딩
TEST(Codec, REQ_COM_001_decode_telemetry) {
  expect_decodes_to_fields("telemetry_nominal");
  const auto v = find("telemetry_nominal");
  const auto payload = from_hex(v.at("payload_hex"));
  const auto t = decode_telemetry(payload.data(), payload.size());
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->mode, static_cast<std::uint8_t>(Mode::Mission));
  EXPECT_EQ(t->status_flags, flags::kGpsValid | flags::kCameraOk);
}

// TC-COM-001-02: 필드 경계값 디코딩 (음수 위경도 포함)
TEST(Codec, REQ_COM_001_decode_bounds) {
  expect_decodes_to_fields("telemetry_bounds_min");
  expect_decodes_to_fields("telemetry_bounds_max");
}

// TC-COM-001-03: 인코딩 결과가 벡터 hex와 바이트 단위로 일치
TEST(Codec, REQ_COM_001_encode_matches_vector) {
  for (const auto& v : telemetry_vectors().at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    EXPECT_EQ(encode_telemetry(fields_of(v)), from_hex(v.at("payload_hex")));
  }
}

// TC-COM-001-04의 codec 부분: 예약 비트는 디코딩 단계에서 손대지 않고 그대로 보존한다.
// (수락 여부와 HMI 전달 시 무시는 검증 단계·IF-03 변환에서 시험)
TEST(Codec, REQ_COM_001_reserved_bits_preserved_by_decoder) {
  expect_decodes_to_fields("telemetry_reserved_bits");
}

// payload 길이가 16이 아니면 디코딩하지 않는다 (검증 규칙 ⑦의 전제)
TEST(Codec, REQ_COM_002_telemetry_wrong_length) {
  const Bytes fifteen(15, 0x00);
  const Bytes seventeen(17, 0x00);
  EXPECT_FALSE(decode_telemetry(fifteen.data(), fifteen.size()).has_value());
  EXPECT_FALSE(decode_telemetry(seventeen.data(), seventeen.size()).has_value());
}
