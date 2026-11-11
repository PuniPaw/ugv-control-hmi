#include "ugvc/gateway/heartbeat.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "support/fake_clock.hpp"
#include "ugvc/codec/crc16.hpp"
#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/msg_type.hpp"

using namespace std::chrono_literals;
using namespace ugvc;
using test::FakeClock;

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
