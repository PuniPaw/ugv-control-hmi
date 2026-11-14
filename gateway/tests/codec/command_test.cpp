#include <gtest/gtest.h>

#include "support/vectors.hpp"
#include "ugvc/codec/command.hpp"

using namespace ugvc::codec;
using ugvc::test::from_hex;
using ugvc::test::load_vectors;

// ADR-002: COMMAND 인코딩이 공용 벡터(Python과 같은 파일)와 바이트 단위로 일치
TEST(Codec, REQ_QUAL_001_command_matches_vectors) {
  for (const auto& v : load_vectors("command.json").at("vectors")) {
    SCOPED_TRACE(v.at("name").get<std::string>());
    const auto& f = v.at("fields");
    const Command c{f.at("cmd_id").get<std::uint32_t>(), f.at("cmd_type").get<std::uint8_t>(),
                    f.at("lat").get<std::int32_t>(), f.at("lon").get<std::int32_t>()};
    EXPECT_EQ(encode_command(c), from_hex(v.at("payload_hex")));
  }
}

TEST(Codec, REQ_CMD_002_cmd_type_names) {
  EXPECT_EQ(parse_cmd_type("STOP"), CmdType::Stop);
  EXPECT_EQ(parse_cmd_type("RTB"), CmdType::Rtb);
  EXPECT_EQ(parse_cmd_type("WAYPOINT"), CmdType::Waypoint);
  EXPECT_FALSE(parse_cmd_type("stop").has_value());
  EXPECT_FALSE(parse_cmd_type("").has_value());
}
