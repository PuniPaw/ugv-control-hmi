#include "ugvc/codec/frame.hpp"

#include <gtest/gtest.h>

#include "support/vectors.hpp"
#include "ugvc/codec/crc16.hpp"

using namespace ugvc::codec;
using ugvc::test::from_hex;
using ugvc::test::load_vectors;

namespace {

const nlohmann::json& frame_vectors() {
  static const nlohmann::json doc = load_vectors("frame.json");  // 한 번만 읽는다
  return doc;
}

}  // namespace

// 공용 벡터의 CRC 기준값이 C++ 구현과 일치 (TC-COM-002-02와 같은 값을 벡터 파일로도 확인)
TEST(Frame, REQ_COM_002_vector_file_crc_check) {
  const auto& c = frame_vectors().at("crc_check");
  const std::string ascii = c.at("ascii");
  const auto crc = crc16_ccitt_false(reinterpret_cast<const std::uint8_t*>(ascii.data()),
                                     ascii.size());
  EXPECT_EQ(crc, std::stoul(c.at("crc16").get<std::string>(), nullptr, 16));
}

// ADR-002: C++ 인코딩이 Python 표준 라이브러리로 만든 바이트와 같아야 한다
TEST(Frame, REQ_QUAL_001_encode_matches_vectors) {
  for (const auto& v : frame_vectors().at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const auto payload = from_hex(v.at("payload_hex"));
    const auto frame = encode_frame(v.at("msg_type").get<std::uint8_t>(),
                                    v.at("seq").get<std::uint32_t>(),
                                    v.at("timestamp_ms").get<std::uint64_t>(),
                                    payload.data(), payload.size());
    EXPECT_EQ(frame, from_hex(v.at("hex")));
  }
}

// 벡터 바이트를 디코딩하면 원래 헤더 필드가 나와야 한다
TEST(Frame, REQ_QUAL_001_decode_header_matches_vectors) {
  for (const auto& v : frame_vectors().at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const auto bytes = from_hex(v.at("hex"));
    const auto h = decode_header(bytes.data(), bytes.size());
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->sync, kSync);
    EXPECT_EQ(h->version, kVersion);
    EXPECT_EQ(h->msg_type, v.at("msg_type").get<std::uint8_t>());
    EXPECT_EQ(h->seq, v.at("seq").get<std::uint32_t>());
    EXPECT_EQ(h->timestamp_ms, v.at("timestamp_ms").get<std::uint64_t>());
    EXPECT_EQ(h->payload_len, from_hex(v.at("payload_hex")).size());
    EXPECT_EQ(bytes.size(), kFrameOverhead + h->payload_len);
  }
}

// 프레임의 CRC 필드 = 헤더~payload의 CRC
TEST(Frame, REQ_COM_002_crc_field_covers_header_and_payload) {
  for (const auto& v : frame_vectors().at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const auto bytes = from_hex(v.at("hex"));
    const auto crc = read_frame_crc(bytes.data(), bytes.size());
    ASSERT_TRUE(crc.has_value());
    EXPECT_EQ(*crc, crc16_ccitt_false(bytes.data(), bytes.size() - kCrcSize));
  }
}

// 길이가 모자라면 헤더·CRC를 읽지 않는다 (검증 규칙 ①의 전제)
TEST(Frame, REQ_COM_002_short_input_returns_nullopt) {
  const Bytes seventeen(17, 0x00);
  EXPECT_FALSE(decode_header(seventeen.data(), seventeen.size()).has_value());
  const Bytes nineteen(19, 0x00);
  EXPECT_TRUE(decode_header(nineteen.data(), nineteen.size()).has_value());
  EXPECT_FALSE(read_frame_crc(nineteen.data(), nineteen.size()).has_value());
}

// 인코딩 → 디코딩 왕복
TEST(Frame, REQ_QUAL_001_header_roundtrip) {
  const std::uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
  const auto frame = encode_frame(0x11, 0x01020304, 0x1122334455667788ULL, payload, sizeof payload);
  const auto h = decode_header(frame.data(), frame.size());
  ASSERT_TRUE(h.has_value());
  Header expected;
  expected.sync = kSync;
  expected.version = kVersion;
  expected.msg_type = 0x11;
  expected.seq = 0x01020304;
  expected.timestamp_ms = 0x1122334455667788ULL;
  expected.payload_len = sizeof payload;
  EXPECT_EQ(*h, expected);
}
