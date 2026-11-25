// DB 행 변환: 유효 TELEMETRY·DETECTION과 이벤트(링크 전이, 명령)를 저장할 값으로 (REQ-LOG-001)
#include "ugvc/dblog/row.hpp"

#include <limits>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using namespace ugvc;
using dblog::Row;
using dblog::Table;
using nlohmann::json;

std::string at(const Row& r, std::size_t i) {
  EXPECT_TRUE(r.values.at(i).has_value()) << "column " << i;
  return r.values.at(i).value_or("");
}

json detail_of(const Row& r) { return json::parse(at(r, 3)); }

codec::Header header(std::uint32_t seq, std::uint64_t ts) {
  codec::Header h;
  h.seq = seq;
  h.timestamp_ms = ts;
  return h;
}

}  // namespace

TEST(DbRow, REQ_LOG_001_telemetry_row_keeps_raw_icd_values) {
  const codec::Telemetry t{-123456789, 1271234567, 35999, 3000, 7, 2, 0x000B};
  const Row r = dblog::telemetry_row(header(42, std::numeric_limits<std::uint64_t>::max()), t, 1'800'000'000'123);
  EXPECT_EQ(r.table, Table::Telemetry);
  const std::vector<std::string> expected{"1800000000123", "42",    "18446744073709551615", "-123456789", "1271234567",
                                          "35999",         "3000",  "7",                    "2",          "11"};
  ASSERT_EQ(r.values.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_EQ(at(r, i), expected[i]) << "column " << i;
}

TEST(DbRow, REQ_LOG_001_detection_row_keeps_every_object) {
  codec::Detection d;
  d.frame_id = 4'000'000'000U;
  d.capture_ts = 1'800'000'000'000;
  d.img_w = 1280;
  d.img_h = 720;
  d.objects = {{0, 9500, 10, 20, 30, 40}, {255, 1, 0, 0, 1, 1}};
  const Row r = dblog::detection_row(header(7, 99), d, 1'800'000'000'050);
  EXPECT_EQ(r.table, Table::Detection);
  EXPECT_EQ(at(r, 0), "1800000000050");
  EXPECT_EQ(at(r, 1), "7");
  EXPECT_EQ(at(r, 2), "99");
  EXPECT_EQ(at(r, 3), "4000000000");
  EXPECT_EQ(at(r, 4), "1800000000000");
  EXPECT_EQ(at(r, 5), "1280");
  EXPECT_EQ(at(r, 6), "720");
  EXPECT_EQ(json::parse(at(r, 7)),
            json::parse(R"([{"class_id":0,"confidence":9500,"x":10,"y":20,"w":30,"h":40},
                            {"class_id":255,"confidence":1,"x":0,"y":0,"w":1,"h":1}])"));
}

TEST(DbRow, REQ_LOG_001_link_transition_is_an_event_with_source) {
  const gateway::LinkTransition t{ingress::Source::Detector,
                                  {link::LinkState::Degraded, link::LinkState::Lost, 1000, 400}};
  const Row r = dblog::link_row(t);
  EXPECT_EQ(r.table, Table::Event);
  EXPECT_EQ(at(r, 0), "1000");
  EXPECT_EQ(at(r, 1), "link");
  EXPECT_EQ(at(r, 2), "DETECTOR");
  EXPECT_EQ(detail_of(r), json::parse(R"({"from":"DEGRADED","to":"LOST","last_rx_ts":400})"));

  const Row first = dblog::link_row({ingress::Source::Vehicle, {link::LinkState::Lost, link::LinkState::Connected, 5, {}}});
  EXPECT_EQ(detail_of(first)["last_rx_ts"], nullptr);
}

TEST(DbRow, REQ_LOG_001_every_command_event_kind_is_stored) {
  using codec::CmdType;
  const Row sent = dblog::command_row(gateway::CommandSent{"a", 10, CmdType::Waypoint, 2, 375000000, -1270000000}, 77);
  EXPECT_EQ(at(sent, 0), "77");
  EXPECT_EQ(at(sent, 1), "cmd_sent");
  EXPECT_FALSE(sent.values.at(2).has_value());  // 명령 이벤트는 source NULL
  EXPECT_EQ(detail_of(sent),
            json::parse(R"({"req_id":"a","cmd_id":10,"cmd_type":"WAYPOINT","attempt":2,"lat":375000000,"lon":-1270000000})"));

  const Row stop = dblog::command_row(gateway::CommandSent{"s", 11, CmdType::Stop, 1}, 77);
  EXPECT_FALSE(detail_of(stop).contains("lat"));

  const Row result = dblog::command_row(gateway::CommandResult{"b", std::nullopt, gateway::CmdResult::RejectedInvalid, 0, true}, 78);
  EXPECT_EQ(at(result, 1), "cmd_result");
  EXPECT_EQ(detail_of(result),
            json::parse(R"({"req_id":"b","cmd_id":null,"result":"REJECTED_INVALID","attempts":0,"repeated":true})"));

  const Row ignored = dblog::command_row(gateway::CommandIgnored{"c", "req_id pending"}, 79);
  EXPECT_EQ(at(ignored, 1), "cmd_ignored");
  EXPECT_EQ(detail_of(ignored), json::parse(R"({"req_id":"c","reason":"req_id pending"})"));

  const Row unknown = dblog::command_row(gateway::UnknownAck{12, 9}, 80);
  EXPECT_EQ(at(unknown, 1), "cmd_unknown_ack");
  EXPECT_EQ(detail_of(unknown), json::parse(R"({"cmd_id":12,"result":9})"));
}

// PostgreSQL text·jsonb는 NUL을 받지 않는다: HMI가 NUL이 든 req_id를 보내도 그 행이 배치를 깨지 않게 바꿔 둔다
TEST(DbRow, REQ_LOG_001_nul_in_req_id_is_replaced_before_storing) {
  const Row r = dblog::command_row(gateway::CommandIgnored{std::string("x\0y", 3), "req_id pending"}, 1);
  const std::string detail = at(r, 3);
  EXPECT_EQ(detail.find("\\u0000"), std::string::npos);
  EXPECT_EQ(detail_of(r)["req_id"], "x\xEF\xBF\xBDy");
}

TEST(DbRow, REQ_LOG_001_row_width_matches_insert_statement) {
  const codec::Telemetry t{};
  const codec::Detection d{};
  const std::vector<Row> rows{dblog::telemetry_row({}, t, 0), dblog::detection_row({}, d, 0),
                              dblog::command_row(gateway::UnknownAck{1, 1}, 0)};
  for (const auto& r : rows) {
    const std::string sql = dblog::insert_sql(r.table);
    const int n = dblog::column_count(r.table);
    EXPECT_EQ(r.values.size(), static_cast<std::size_t>(n));
    EXPECT_NE(sql.find("$" + std::to_string(n) + ")"), std::string::npos) << sql;
    EXPECT_EQ(sql.find("$" + std::to_string(n + 1)), std::string::npos) << sql;
  }
}

