#include "ugvc/gateway/gateway.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "support/fake_clock.hpp"
#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/crc16.hpp"
#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/gateway/heartbeat.hpp"
#include "ugvc/ingress/validator.hpp"

using namespace std::chrono_literals;
using namespace ugvc;
using ingress::RejectReason;
using ingress::Source;
using link::LinkState;
using test::FakeClock;

namespace {

struct NullSink final : link::IEventSink {
  void on_link_transition(const link::LinkTransition&) override {}
};

codec::Bytes telemetry_frame(std::uint32_t seq) {
  codec::Telemetry t;
  t.battery = 50;
  const auto payload = codec::encode_telemetry(t);
  return codec::encode_frame(codec::kMsgTelemetry, seq, 0, payload.data(), payload.size());
}

codec::Bytes ack_frame(std::uint32_t seq) {
  const auto payload = codec::encode_ack({1, 0});
  return codec::encode_frame(codec::kMsgAck, seq, 0, payload.data(), payload.size());
}

struct GatewayFixture : ::testing::Test {
  FakeClock clock;
  NullSink sink;
  gateway::Gateway gw{clock, sink};

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
  codec::Detection d;
  d.img_w = 640;
  d.img_h = 480;
  const auto det_payload = codec::encode_detection(d);
  auto det = [&](std::uint32_t seq) {
    return codec::encode_frame(codec::kMsgDetection, seq, 0, det_payload.data(), det_payload.size());
  };
  ASSERT_TRUE(send(telemetry_frame(0)));
  ASSERT_TRUE(send(det(0), Source::Detector));
  for (std::uint32_t i = 1; i <= 15; ++i) {  // 1.5s 동안 DET만 계속 수신
    clock.advance(100ms);
    ASSERT_TRUE(send(det(i), Source::Detector));
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

// TC-COM-005-07: 스트림 독립. TELEMETRY seq 건너뜀이 ACK 스트림에 영향 없음
TEST_F(GatewayFixture, REQ_COM_005_streams_independent) {
  ASSERT_TRUE(send(telemetry_frame(0)));
  ASSERT_TRUE(send(ack_frame(0)));
  ASSERT_TRUE(send(telemetry_frame(5)));  // TELEMETRY 유실 4
  ASSERT_TRUE(send(ack_frame(1)));        // ACK는 연속
  ASSERT_TRUE(send(ack_frame(2)));
  const auto s = gw.stats(Source::Vehicle);
  EXPECT_EQ(s.lost_count, 4U);
  EXPECT_EQ(s.rx_count, 5U);
  EXPECT_EQ(s.errors[RejectReason::Order], 0U);  // ACK seq 0,1,2가 TELEMETRY seq 5와 비교되지 않음
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

// TC-COM-007-01: 1Hz HEARTBEAT, seq 0·1·2, payload 0, 검증 ①~⑤ 통과 형식
TEST(Heartbeat, REQ_COM_007_emits_1hz) {
  FakeClock clock;
  gateway::HeartbeatSender hb(clock);
  std::vector<codec::Bytes> sent;
  for (int i = 0; i < 59; ++i) {  // 50ms 판정 주기로 2.95s
    if (auto f = hb.poll()) sent.push_back(*f);
    clock.advance(50ms);
  }
  ASSERT_EQ(sent.size(), 3U);
  for (std::uint32_t i = 0; i < 3; ++i) {
    const auto h = codec::decode_header(sent[i].data(), sent[i].size());
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->msg_type, gateway::kMsgHeartbeat);
    EXPECT_EQ(h->payload_len, 0U);
    EXPECT_EQ(h->seq, i);
    EXPECT_EQ(sent[i].size(), codec::kFrameOverhead);
    const auto crc = codec::read_frame_crc(sent[i].data(), sent[i].size());
    EXPECT_EQ(*crc, codec::crc16_ccitt_false(sent[i].data(), sent[i].size() - codec::kCrcSize));
  }
}

// TC-COM-007-02: 차량 링크가 LOST여도 계속 보낸다 (HeartbeatSender는 링크 상태를 모른다)
TEST(Heartbeat, REQ_COM_007_sent_while_lost) {
  FakeClock clock;
  NullSink sink;
  gateway::Gateway gw(clock, sink);
  gateway::HeartbeatSender hb(clock);
  int sent = 0;
  for (int i = 0; i < 100; ++i) {  // 5s, 차량 패킷 없음
    gw.poll();
    if (hb.poll()) ++sent;
    clock.advance(50ms);
  }
  EXPECT_EQ(gw.link(Source::Vehicle).state(), LinkState::Lost);
  EXPECT_EQ(sent, 5);
}

// TC-COM-005-08: stats 1Hz. 판정 주기가 늦어져도 몰아서 내지 않는다
TEST(Periodic, REQ_COM_005_emits_1hz) {
  FakeClock clock;
  core::Periodic p(clock, 1s);
  int fired = 0;
  for (int i = 0; i < 60; ++i) {  // 3.0s
    if (p.due()) ++fired;
    clock.advance(50ms);
  }
  EXPECT_EQ(fired, 3);
  clock.advance(5s);  // 5초 멈췄다가
  EXPECT_TRUE(p.due());
  EXPECT_FALSE(p.due());  // 밀린 4번을 한꺼번에 내지 않는다
}

// 호출 간격이 주기를 나누어떨어지지 않아도(300ms) 주기가 밀리지 않는다: 10초에 정확히 10회
TEST(Periodic, REQ_COM_007_no_drift_with_uneven_polling) {
  FakeClock clock;
  core::Periodic p(clock, 1s);
  int fired = 0;
  for (int i = 0; i < 34; ++i) {  // 0 ~ 9.9s
    if (p.due()) ++fired;
    clock.advance(300ms);
  }
  EXPECT_EQ(fired, 10);
}
