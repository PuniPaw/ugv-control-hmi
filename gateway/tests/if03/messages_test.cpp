#include "ugvc/if03/messages.hpp"

#include <gtest/gtest.h>

using namespace ugvc;
using nlohmann::json;

// ICD 7.1 예시와 같은 값 → 같은 공학 단위
TEST(If03, REQ_COM_001_telemetry_engineering_units) {
  codec::Header h;
  h.seq = 1532;
  const codec::Telemetry t{361234567, 1277654321, 8725, 450, 76, 1, 0b11};
  const json m = if03::telemetry(h, t, 1794729600118ULL);
  EXPECT_EQ(m["type"], "telemetry");
  EXPECT_EQ(m["ts"], 1794729600118ULL);
  const auto& d = m["data"];
  EXPECT_NEAR(d["lat"].get<double>(), 36.1234567, 1e-9);
  EXPECT_NEAR(d["lon"].get<double>(), 127.7654321, 1e-9);
  EXPECT_NEAR(d["heading_deg"].get<double>(), 87.25, 1e-9);
  EXPECT_NEAR(d["speed_mps"].get<double>(), 4.5, 1e-9);
  EXPECT_EQ(d["battery_pct"], 76);
  EXPECT_EQ(d["mode"], "MISSION");
  EXPECT_EQ(d["flags"], json({"GPS_VALID", "CAMERA_OK"}));
  EXPECT_EQ(d["seq"], 1532);
}

// TC-COM-001-04의 HMI 전달 부분: 예약 비트는 flags에 나타나지 않는다
TEST(If03, REQ_COM_001_reserved_bits_not_relayed) {
  const codec::Telemetry t{0, 0, 0, 0, 50, 0, 0xFFF3};
  const json m = if03::telemetry({}, t, 0);
  EXPECT_EQ(m["data"]["flags"], json({"GPS_VALID", "CAMERA_OK"}));
}

TEST(If03, REQ_COM_005_stats_fields) {
  gateway::SourceStats s;
  s.rx_count = 600;
  s.lost_count = 3;
  s.loss_rate_10s = 0.005;
  s.errors.add(ingress::RejectReason::Crc);
  const json m = if03::stats(ingress::Source::Vehicle, s, 42);
  EXPECT_EQ(m["type"], "stats");
  EXPECT_EQ(m["data"]["source"], "VEHICLE");
  EXPECT_EQ(m["data"]["rx_count"], 600);
  EXPECT_EQ(m["data"]["lost_count"], 3);
  EXPECT_DOUBLE_EQ(m["data"]["loss_rate_10s"].get<double>(), 0.005);
  EXPECT_EQ(m["data"]["errors"],
            json({{"sync", 0}, {"version", 0}, {"length", 0}, {"crc", 1}, {"type", 0}, {"range", 0}, {"order", 0}}));
}

// TC-COM-005-06의 HMI 전달 부분: 창이 비면 null
TEST(If03, REQ_COM_005_empty_loss_rate_is_null) {
  const json m = if03::stats(ingress::Source::Detector, {}, 0);
  EXPECT_TRUE(m["data"]["loss_rate_10s"].is_null());
}

TEST(If03, REQ_COM_004_link_status_fields) {
  const json m = if03::link_status(ingress::Source::Detector, link::LinkState::Degraded, 100, 700);
  EXPECT_EQ(m["type"], "link_status");
  EXPECT_EQ(m["data"]["source"], "DETECTOR");
  EXPECT_EQ(m["data"]["state"], "DEGRADED");
  EXPECT_EQ(m["data"]["last_rx_ts"], 100);
  EXPECT_TRUE(if03::link_status(ingress::Source::Vehicle, link::LinkState::Lost, std::nullopt, 0)["data"]["last_rx_ts"].is_null());
}

TEST(If03, REQ_AI_001_detection_fields) {
  codec::Detection d;
  d.frame_id = 42;
  d.capture_ts = 1000;
  d.img_w = 640;
  d.img_h = 480;
  d.objects.push_back({0, 9150, 100, 120, 50, 150});
  const json m = if03::detection(d, 1035);
  EXPECT_EQ(m["type"], "detection");
  EXPECT_EQ(m["data"]["frame_id"], 42);
  EXPECT_EQ(m["data"]["objects"][0]["class"], "PERSON");
  EXPECT_NEAR(m["data"]["objects"][0]["conf"].get<double>(), 0.915, 1e-9);
}
