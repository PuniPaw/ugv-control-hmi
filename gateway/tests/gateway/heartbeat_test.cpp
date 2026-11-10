#include "ugvc/gateway/heartbeat.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "support/fake_clock.hpp"
#include "ugvc/codec/crc16.hpp"
#include "ugvc/gateway/gateway.hpp"

using namespace std::chrono_literals;
using namespace ugvc;
using ingress::Source;
using link::LinkState;
using test::FakeClock;

namespace {

struct NullSink final : link::ILinkTransitionSink {
  void on_link_transition(const link::LinkTransition&) override {}
};

}  // namespace

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
    EXPECT_EQ(h->msg_type, codec::kMsgHeartbeat);
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
