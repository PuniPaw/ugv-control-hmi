#include "ugvc/gateway/gateway.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "support/fake_clock.hpp"
#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/ingress/validator.hpp"

using namespace std::chrono_literals;
using namespace ugvc;
using ingress::RejectReason;
using ingress::Source;
using link::LinkState;
using test::FakeClock;

namespace {

codec::Bytes telemetry_frame(std::uint32_t seq) {
  codec::Telemetry t;
  t.battery = 50;
  const auto payload = codec::encode_telemetry(t);
  return codec::encode_frame(codec::kMsgTelemetry, seq, 0, payload.data(), payload.size());
}

codec::Bytes detection_frame(std::uint32_t seq) {
  codec::Detection d;
  d.img_w = 640;
  d.img_h = 480;
  const auto payload = codec::encode_detection(d);
  return codec::encode_frame(codec::kMsgDetection, seq, 0, payload.data(), payload.size());
}

codec::Bytes ack_frame(std::uint32_t seq) {
  const auto payload = codec::encode_ack({1, 0});
  return codec::encode_frame(codec::kMsgAck, seq, 0, payload.data(), payload.size());
}

struct GatewayFixture : ::testing::Test {
  FakeClock clock;
  gateway::Gateway gw{clock};

  bool send(const codec::Bytes& frame, Source s = Source::Vehicle) {
    return gw.on_datagram(s, frame.data(), frame.size()).has_value();
  }
};

}  // namespace

// TC-COM-003-07: 폐기 패킷(CRC 오류)은 링크 타이머를 갱신하지 않는다
TEST_F(GatewayFixture, REQ_COM_003_rejected_does_not_reset) {
  ASSERT_TRUE(send(telemetry_frame(0)));
  clock.advance(400ms);
  auto bad = telemetry_frame(1);
  bad.back() ^= 0x01;
  EXPECT_FALSE(send(bad));
  clock.advance(100ms);  // 유효 패킷 후 0.5s
  gw.poll();
  EXPECT_EQ(gw.link(Source::Vehicle).state(), LinkState::Degraded);
  EXPECT_EQ(gw.stats(Source::Vehicle).errors[RejectReason::Crc], 1U);
}

// TC-COM-003-08: 송신원 독립
TEST_F(GatewayFixture, REQ_COM_003_sources_independent) {
  ASSERT_TRUE(send(telemetry_frame(0)));
  ASSERT_TRUE(send(detection_frame(0), Source::Detector));
  for (std::uint32_t i = 1; i <= 15; ++i) {  // 1.5s 동안 DET만 계속 수신
    clock.advance(100ms);
    ASSERT_TRUE(send(detection_frame(i), Source::Detector));
  }
  gw.poll();
  EXPECT_EQ(gw.link(Source::Vehicle).state(), LinkState::Lost);
  EXPECT_EQ(gw.link(Source::Detector).state(), LinkState::Connected);
}

// TC-COM-003-09: 같은 송신원의 다른 스트림(ACK)도 링크 타이머를 갱신한다
TEST_F(GatewayFixture, REQ_COM_003_any_stream_refreshes) {
  ASSERT_TRUE(send(telemetry_frame(0)));
  clock.advance(400ms);
  ASSERT_TRUE(send(ack_frame(0)));
  clock.advance(400ms);  // TELEMETRY 후 0.8s, ACK 후 0.4s
  gw.poll();
  EXPECT_EQ(gw.link(Source::Vehicle).state(), LinkState::Connected);
}

// TC-COM-005-07: ACK 스트림은 유실 계산에서 빠진다 (ICD 9장). TELEMETRY 유실만 센다
TEST_F(GatewayFixture, REQ_COM_005_ack_stream_is_not_counted_for_loss) {
  ASSERT_TRUE(send(telemetry_frame(0)));
  ASSERT_TRUE(send(ack_frame(0)));
  ASSERT_TRUE(send(ack_frame(10)));       // ACK seq 건너뜀: 유실 아님
  ASSERT_TRUE(send(telemetry_frame(5)));  // TELEMETRY 유실 4
  const auto s = gw.stats(Source::Vehicle);
  EXPECT_EQ(s.lost_count, 4U);
  EXPECT_EQ(s.rx_count, 4U);
  EXPECT_EQ(s.errors[RejectReason::Order], 0U);
}

// TC-COM-005-09: LOST 후 첫 유효 패킷은 seq 기준값 재설정. 유실·order 변화 없음
TEST_F(GatewayFixture, REQ_COM_005_rebase_after_lost) {
  for (std::uint32_t seq = 5000; seq < 5003; ++seq) ASSERT_TRUE(send(telemetry_frame(seq)));
  clock.advance(1500ms);  // LOST (SIM 재시작 가정)
  EXPECT_TRUE(send(telemetry_frame(0)));
  EXPECT_TRUE(send(telemetry_frame(1)));
  const auto s = gw.stats(Source::Vehicle);
  EXPECT_EQ(s.lost_count, 0U);
  EXPECT_EQ(s.errors[RejectReason::Order], 0U);
  EXPECT_EQ(gw.link(Source::Vehicle).state(), LinkState::Connected);
}

// LOST가 아니면 rebase하지 않는다: 작은 seq는 역순 도착으로 폐기
TEST_F(GatewayFixture, REQ_COM_006_no_rebase_while_connected) {
  ASSERT_TRUE(send(telemetry_frame(5000)));
  clock.advance(900ms);  // DEGRADED, 아직 LOST 아님
  EXPECT_FALSE(send(telemetry_frame(0)));
  EXPECT_EQ(gw.stats(Source::Vehicle).errors[RejectReason::Order], 1U);
}

// TC-COM-006-06: ACK 스트림은 규칙 ⑨ 대상이 아니다. SIM 수신부가 다시 시작해 ACK seq가 0부터 시작해도 받는다 (ICD 9장)
TEST_F(GatewayFixture, REQ_COM_006_ack_stream_is_not_order_checked) {
  for (std::uint32_t seq = 0; seq < 10; ++seq) ASSERT_TRUE(send(telemetry_frame(seq)));
  ASSERT_TRUE(send(ack_frame(41)));
  ASSERT_TRUE(send(ack_frame(42)));
  EXPECT_TRUE(send(ack_frame(0)));  // 차량 링크는 CONNECTED 그대로
  EXPECT_TRUE(send(ack_frame(0)));  // 같은 seq도 받는다 (중복 ACK는 CommandTracker가 처리)
  EXPECT_EQ(gw.stats(Source::Vehicle).errors[RejectReason::Order], 0U);
  EXPECT_FALSE(send(telemetry_frame(3)));  // 텔레메트리 스트림은 여전히 ⑨ 대상
}

// 규칙 ⑨ 위반도 원인별 계수에 들어가고 rx_count는 늘지 않는다
TEST_F(GatewayFixture, REQ_COM_006_order_counted) {
  ASSERT_TRUE(send(telemetry_frame(10)));
  EXPECT_FALSE(send(telemetry_frame(9)));
  EXPECT_FALSE(send(telemetry_frame(10)));
  const auto s = gw.stats(Source::Vehicle);
  EXPECT_EQ(s.errors[RejectReason::Order], 2U);
  EXPECT_EQ(s.rx_count, 1U);
}

// 유실률이 stats에 반영된다 (seq 0, 3 → 수신 2, 유실 2 → 0.5)
TEST_F(GatewayFixture, REQ_COM_005_stats_loss_rate) {
  EXPECT_FALSE(gw.stats(Source::Vehicle).loss_rate_10s.has_value());
  ASSERT_TRUE(send(telemetry_frame(0)));
  ASSERT_TRUE(send(telemetry_frame(3)));
  EXPECT_DOUBLE_EQ(*gw.stats(Source::Vehicle).loss_rate_10s, 0.5);
}

// TC-COM-004-02 보충: 전이는 송신원을 달고 일어난 순서대로 한 번만 꺼내진다 (패킷 처리 중 전이 포함)
TEST_F(GatewayFixture, REQ_COM_004_transitions_carry_source_in_order) {
  EXPECT_TRUE(gw.take_transitions().empty());  // 시작 시 초기 상태(LOST)는 전이가 아니다
  ASSERT_TRUE(send(detection_frame(0), Source::Detector));
  clock.advance(1200ms);
  ASSERT_TRUE(send(telemetry_frame(1), Source::Vehicle));
  ASSERT_TRUE(send(detection_frame(1), Source::Detector));  // 처리 직전 재판정: LOST → 바로 CONNECTED

  const auto t = gw.take_transitions();
  ASSERT_EQ(t.size(), 4U);
  const std::pair<Source, LinkState> expected[] = {{Source::Detector, LinkState::Connected},
                                                   {Source::Vehicle, LinkState::Connected},
                                                   {Source::Detector, LinkState::Lost},
                                                   {Source::Detector, LinkState::Connected}};
  for (std::size_t i = 0; i < t.size(); ++i) {
    EXPECT_EQ(t[i].source, expected[i].first) << i;
    EXPECT_EQ(t[i].change.to, expected[i].second) << i;
  }
  EXPECT_TRUE(gw.take_transitions().empty());  // 꺼내면 비워진다
}

// 유효 패킷의 수신 시각은 링크 상태의 마지막 수신 시각과 같은 값이다 (IF-03 rx_ts와 last_rx_ts가 어긋나지 않게)
TEST_F(GatewayFixture, REQ_COM_001_accepted_rx_time_matches_link) {
  clock.advance(1234ms);
  const auto frame = telemetry_frame(0);
  const auto accepted = gw.on_datagram(Source::Vehicle, frame.data(), frame.size());
  ASSERT_TRUE(accepted.has_value());
  EXPECT_EQ(accepted->rx_epoch_ms, clock.epoch_ms());
  EXPECT_EQ(gw.link(Source::Vehicle).last_rx_epoch_ms(), accepted->rx_epoch_ms);
}
