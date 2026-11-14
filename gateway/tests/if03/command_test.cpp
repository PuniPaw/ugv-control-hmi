#include "ugvc/if03/command.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ugvc/gateway/command_tracker.hpp"
#include "ugvc/if03/messages.hpp"

using namespace ugvc;
using nlohmann::json;

namespace {

std::string cmd_json(const std::string& data) { return R"({"type":"command","ts":1,"data":)" + data + "}"; }

const if03::CommandRequest& ok(const std::variant<if03::CommandRequest, if03::CommandParseError>& r) {
  EXPECT_TRUE(std::holds_alternative<if03::CommandRequest>(r))
      << (std::holds_alternative<if03::CommandParseError>(r) ? std::get<if03::CommandParseError>(r).reason : "");
  return std::get<if03::CommandRequest>(r);
}

if03::CommandParseError err(const std::variant<if03::CommandRequest, if03::CommandParseError>& r) {
  EXPECT_TRUE(std::holds_alternative<if03::CommandParseError>(r));
  return std::get<if03::CommandParseError>(r);
}

}  // namespace

TEST(If03Command, REQ_CMD_002_stop_and_rtb_ignore_coordinates) {
  const auto stop = if03::parse_command(cmd_json(R"({"req_id":"a","cmd_type":"STOP"})"));
  EXPECT_EQ(ok(stop).type, codec::CmdType::Stop);
  EXPECT_EQ(ok(stop).req_id, "a");
  const auto rtb = if03::parse_command(cmd_json(R"({"req_id":"b","cmd_type":"RTB","lat":999,"lon":"x"})"));
  EXPECT_EQ(ok(rtb).type, codec::CmdType::Rtb);
  EXPECT_EQ(ok(rtb).lat, 0);
  EXPECT_EQ(ok(rtb).lon, 0);
}

TEST(If03Command, REQ_CMD_002_ts_is_optional) {
  const auto r = if03::parse_command(R"({"type":"command","data":{"req_id":"a","cmd_type":"STOP"}})");
  EXPECT_EQ(ok(r).req_id, "a");
}

TEST(If03Command, REQ_CMD_002_waypoint_converts_to_e7_with_rounding) {
  const auto r = if03::parse_command(
      cmd_json(R"({"req_id":"w","cmd_type":"WAYPOINT","lat":36.12400004,"lon":-127.76600006})"));
  EXPECT_EQ(ok(r).type, codec::CmdType::Waypoint);
  EXPECT_EQ(ok(r).lat, 361240000);  // 36.12400004 -> 361240000.4 -> 내림
  EXPECT_EQ(ok(r).lon, -1277660001);  // -127.76600006 -> -1277660000.6 -> 반올림(0에서 먼 쪽)
}

TEST(If03Command, REQ_CMD_002_waypoint_accepts_range_limits_and_integers) {
  const auto r = if03::parse_command(cmd_json(R"({"req_id":"w","cmd_type":"WAYPOINT","lat":-90,"lon":180})"));
  EXPECT_EQ(ok(r).lat, -900000000);
  EXPECT_EQ(ok(r).lon, 1800000000);
}

TEST(If03Command, REQ_CMD_002_waypoint_out_of_range_or_missing_or_non_finite_is_rejected_with_req_id) {
  const char* bad[] = {
      R"({"req_id":"w","cmd_type":"WAYPOINT","lat":90.0000001,"lon":0})",
      R"({"req_id":"w","cmd_type":"WAYPOINT","lat":0,"lon":-180.5})",
      R"({"req_id":"w","cmd_type":"WAYPOINT","lat":0})",
      R"({"req_id":"w","cmd_type":"WAYPOINT"})",
      R"({"req_id":"w","cmd_type":"WAYPOINT","lat":"36","lon":127})",
      R"({"req_id":"w","cmd_type":"WAYPOINT","lat":null,"lon":127})",
      R"({"req_id":"w","cmd_type":"WAYPOINT","lat":true,"lon":127})",
  };
  for (const char* data : bad) {
    SCOPED_TRACE(data);
    const auto e = err(if03::parse_command(cmd_json(data)));
    EXPECT_EQ(e.req_id, std::optional<std::string>("w"));
  }
}

// 파서가 JSON 단계에서 거부하므로 req_id를 읽지 못한다 (NaN·Infinity 리터럴도 JSON이 아니다)
TEST(If03Command, REQ_CMD_002_overflowing_or_nan_number_is_rejected) {
  for (const char* data : {R"({"req_id":"w","cmd_type":"WAYPOINT","lat":1e999,"lon":0})",
                           R"({"req_id":"w","cmd_type":"WAYPOINT","lat":NaN,"lon":0})"}) {
    SCOPED_TRACE(data);
    EXPECT_FALSE(err(if03::parse_command(cmd_json(data))).req_id.has_value());
  }
}

TEST(If03Command, REQ_CMD_002_unknown_or_non_string_cmd_type_is_rejected_with_req_id) {
  for (const char* data : {R"({"req_id":"x","cmd_type":"FIRE"})", R"({"req_id":"x","cmd_type":"stop"})",
                           R"({"req_id":"x","cmd_type":1})", R"({"req_id":"x"})"}) {
    SCOPED_TRACE(data);
    EXPECT_EQ(err(if03::parse_command(cmd_json(data))).req_id, std::optional<std::string>("x"));
  }
}

TEST(If03Command, REQ_CMD_002_bad_req_id_is_rejected_without_req_id) {
  const std::string too_long(65, 'a');
  for (const std::string& data : std::vector<std::string>{R"({"cmd_type":"STOP"})", R"({"req_id":"","cmd_type":"STOP"})",
                                 R"({"req_id":7,"cmd_type":"STOP"})",
                                 R"({"req_id":")" + too_long + R"(","cmd_type":"STOP"})"}) {
    SCOPED_TRACE(data);
    EXPECT_FALSE(err(if03::parse_command(cmd_json(data))).req_id.has_value());
  }
  const std::string longest(64, 'a');
  EXPECT_EQ(ok(if03::parse_command(cmd_json(R"({"req_id":")" + longest + R"(","cmd_type":"STOP"})"))).req_id, longest);
}

TEST(If03Command, REQ_CMD_002_malformed_input_is_rejected_without_req_id) {
  const std::string huge = cmd_json(R"({"req_id":"a","cmd_type":"STOP","pad":")" + std::string(5000, 'x') + R"("})");
  for (const std::string& text : {std::string("not json"), std::string(""), std::string("[1,2]"), std::string("42"),
                                 std::string(R"({"type":"ping"})"), std::string(R"({"type":"command"})"),
                                 std::string(R"({"type":"command","data":[]})"), huge}) {
    SCOPED_TRACE(text.substr(0, 60));
    EXPECT_FALSE(err(if03::parse_command(text)).req_id.has_value());
  }
}

TEST(If03Command, REQ_CMD_002_cmd_result_json_shape) {
  const json m = if03::cmd_result({"r1", 42u, gateway::CmdResult::Timeout, 4}, 1794729600118ULL);
  EXPECT_EQ(m, json::parse(R"({"type":"cmd_result","ts":1794729600118,
      "data":{"req_id":"r1","cmd_id":42,"result":"TIMEOUT","attempts":4}})"));
}

TEST(If03Command, REQ_CMD_002_cmd_result_without_cmd_id_is_null) {
  const json m = if03::cmd_result({"r1", std::nullopt, gateway::CmdResult::RejectedInvalid, 0}, 5);
  EXPECT_TRUE(m["data"]["cmd_id"].is_null());
  EXPECT_EQ(m["data"]["result"], "REJECTED_INVALID");
  EXPECT_EQ(m["data"]["attempts"], 0);
}

TEST(If03Command, REQ_CMD_002_cmd_result_names_all_results) {
  using gateway::CmdResult;
  const std::pair<CmdResult, const char*> all[] = {{CmdResult::Accepted, "ACCEPTED"},
                                                   {CmdResult::RejectedInvalid, "REJECTED_INVALID"},
                                                   {CmdResult::RejectedState, "REJECTED_STATE"},
                                                   {CmdResult::Timeout, "TIMEOUT"}};
  for (const auto& [r, name] : all) EXPECT_EQ(if03::cmd_result({"r", 1u, r, 1}, 0)["data"]["result"], name);
}
