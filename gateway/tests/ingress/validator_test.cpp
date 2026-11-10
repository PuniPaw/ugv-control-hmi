#include "ugvc/ingress/validator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "support/vectors.hpp"

using namespace ugvc::ingress;
using ugvc::test::from_hex;
using ugvc::test::load_vectors;

namespace {

const nlohmann::json& cases() {
  static const nlohmann::json doc = load_vectors("validation.json");
  return doc.at("cases");
}

Source source_of(const nlohmann::json& c) {
  return c.at("source") == "VEHICLE" ? Source::Vehicle : Source::Detector;
}

std::string verdict(const CheckResult& r) {
  if (std::holds_alternative<CheckedFrame>(r)) return "accept";
  return to_string(std::get<RejectReason>(r));
}

// 이 시험 파일이 다루는 TC. validation.json의 모든 case는 이 중 하나여야 한다.
const std::set<std::string> kCoveredTcs = {
    "TC-COM-001-04", "TC-COM-002-01", "TC-COM-002-03", "TC-COM-002-04", "TC-COM-002-05",
    "TC-COM-002-06", "TC-COM-002-07", "TC-COM-002-08", "TC-COM-002-09", "TC-COM-002-10",
    "TC-COM-002-11", "TC-COM-002-12", "TC-COM-002-13",
};

// tc에 속한 case 전부가 expect대로 판정되는지. 적어도 1건은 있어야 한다.
void expect_cases(const std::string& tc) {
  int n = 0;
  for (const auto& c : cases()) {
    if (c.at("tc") != tc) continue;
    ++n;
    SCOPED_TRACE(c.at("name").get<std::string>());
    const auto bytes = from_hex(c.at("hex"));
    EXPECT_EQ(verdict(check_frame(source_of(c), bytes.data(), bytes.size())),
              c.at("expect").get<std::string>());
  }
  EXPECT_GT(n, 0) << "no vectors for " << tc;
}

}  // namespace

TEST(Validator, AllVectorCasesAreCovered) {
  for (const auto& c : cases()) {
    EXPECT_TRUE(kCoveredTcs.count(c.at("tc").get<std::string>()) == 1)
        << c.at("name") << " has unhandled tc " << c.at("tc");
  }
}

TEST(Validator, REQ_COM_002_accepts_valid) { expect_cases("TC-COM-002-01"); }
TEST(Validator, REQ_COM_001_reserved_bits_ignored) { expect_cases("TC-COM-001-04"); }
TEST(Validator, REQ_COM_002_rule1_too_short) { expect_cases("TC-COM-002-03"); }
TEST(Validator, REQ_COM_002_rule2_bad_sync) { expect_cases("TC-COM-002-04"); }
TEST(Validator, REQ_COM_002_rule3_bad_version) { expect_cases("TC-COM-002-05"); }
TEST(Validator, REQ_COM_002_rule4_len_mismatch) { expect_cases("TC-COM-002-06"); }
TEST(Validator, REQ_COM_002_rule5_bad_crc) { expect_cases("TC-COM-002-07"); }
TEST(Validator, REQ_COM_002_rule6_type_not_allowed) { expect_cases("TC-COM-002-08"); }
TEST(Validator, REQ_COM_002_rule7_type_length) { expect_cases("TC-COM-002-09"); }
TEST(Validator, REQ_COM_002_rule8_telemetry_range) { expect_cases("TC-COM-002-10"); }
TEST(Validator, REQ_COM_002_rule8_detection_range) { expect_cases("TC-COM-002-11"); }
TEST(Validator, REQ_COM_002_rule8_ack_range) { expect_cases("TC-COM-002-12"); }
TEST(Validator, REQ_COM_002_first_failure_only) { expect_cases("TC-COM-002-13"); }

// 통과한 프레임의 payload 포인터는 헤더 바로 뒤를 가리킨다
TEST(Validator, REQ_COM_002_accepted_frame_points_at_payload) {
  for (const auto& c : cases()) {
    if (c.at("expect") != "accept") continue;
    SCOPED_TRACE(c.at("name").get<std::string>());
    const auto bytes = from_hex(c.at("hex"));
    const auto r = check_frame(source_of(c), bytes.data(), bytes.size());
    ASSERT_TRUE(std::holds_alternative<CheckedFrame>(r));
    EXPECT_EQ(std::get<CheckedFrame>(r).payload, bytes.data() + ugvc::codec::kHeaderSize);
  }
}

// TC-COM-002-14: 원인별 계수가 독립적으로 정확히 누적된다
TEST(Validator, REQ_COM_002_counters_accumulate) {
  auto first_with = [](const std::string& expect) {
    const auto it = std::find_if(cases().begin(), cases().end(),
                                 [&](const nlohmann::json& c) { return c.at("expect") == expect; });
    if (it == cases().end()) throw std::runtime_error("no case expecting " + expect);
    return *it;
  };
  const auto crc_case = first_with("crc");
  const auto sync_case = first_with("sync");

  ErrorCounters counters;
  auto feed = [&](const nlohmann::json& c) {
    const auto bytes = from_hex(c.at("hex"));
    const auto r = check_frame(source_of(c), bytes.data(), bytes.size());
    if (auto* reason = std::get_if<RejectReason>(&r)) counters.add(*reason);
  };
  for (int i = 0; i < 3; ++i) feed(crc_case);
  feed(sync_case);

  EXPECT_EQ(counters[RejectReason::Crc], 3U);
  EXPECT_EQ(counters[RejectReason::Sync], 1U);
  EXPECT_EQ(counters[RejectReason::Length], 0U);
  EXPECT_EQ(counters.total(), 4U);
}
