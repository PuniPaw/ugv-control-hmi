#include "ugvc/codec/route.hpp"

#include <gtest/gtest.h>

#include "support/vectors.hpp"

using namespace ugvc::codec;
using ugvc::test::from_hex;
using ugvc::test::load_vectors;

namespace {

Route route_of(const nlohmann::json& f) {
  Route r;
  for (const auto& w : f.at("waypoints")) {
    r.waypoints.push_back({w.at("lat").get<std::int32_t>(), w.at("lon").get<std::int32_t>()});
  }
  return r;
}

}  // namespace

// TC-CMD-007-03: ROUTE 인코딩·디코딩이 벡터와 바이트 단위로 일치 (count 0, 32, 좌표 경계 포함)
TEST(Codec, REQ_CMD_007_route_matches_vectors) {
  for (const auto& v : load_vectors("route.json").at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const Route expected = route_of(v.at("fields"));
    const auto payload = from_hex(v.at("payload_hex"));
    EXPECT_EQ(encode_route(expected), payload);
    const auto decoded = decode_route(payload.data(), payload.size());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, expected);
  }
}
