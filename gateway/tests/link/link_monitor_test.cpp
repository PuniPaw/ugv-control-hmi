#include "ugvc/link/link_monitor.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "support/fake_clock.hpp"

using namespace std::chrono_literals;
using namespace ugvc::link;
using ugvc::ingress::Source;
using ugvc::test::FakeClock;

namespace {

struct RecordingSink final : ILinkTransitionSink {
  void on_link_transition(const LinkTransition& t) override { events.push_back(t); }
  std::vector<LinkTransition> events;
};

// 패킷 하나를 받은 직후 상태에서 시작하는 공통 준비
struct LinkFixture : ::testing::Test {
  FakeClock clock;
  RecordingSink sink;
  LinkMonitor link{Source::Vehicle, clock, sink};

  LinkState after(std::chrono::milliseconds elapsed) {
    link.on_valid_packet();
    clock.advance(elapsed);
    link.poll();
    return link.state();
  }
};

}  // namespace

// TC-COM-003-01
TEST_F(LinkFixture, REQ_COM_003_initially_lost) {
  EXPECT_EQ(link.state(), LinkState::Lost);
  clock.advance(5s);
  link.poll();
  EXPECT_EQ(link.state(), LinkState::Lost);
  EXPECT_FALSE(link.last_rx_epoch_ms().has_value());
}

// TC-COM-003-02 ~ 05: 경계값
TEST_F(LinkFixture, REQ_COM_003_connected_before_500ms) { EXPECT_EQ(after(499ms), LinkState::Connected); }
TEST_F(LinkFixture, REQ_COM_003_degraded_at_500ms) { EXPECT_EQ(after(500ms), LinkState::Degraded); }
TEST_F(LinkFixture, REQ_COM_003_degraded_before_1s) { EXPECT_EQ(after(999ms), LinkState::Degraded); }
TEST_F(LinkFixture, REQ_COM_003_lost_at_1s) { EXPECT_EQ(after(1000ms), LinkState::Lost); }

// TC-COM-003-06
TEST_F(LinkFixture, REQ_COM_003_valid_packet_resets) {
  EXPECT_EQ(after(700ms), LinkState::Degraded);
  EXPECT_EQ(after(499ms), LinkState::Connected);  // 새 패킷 기준으로 다시 499ms
}

// TC-COM-004-01
TEST_F(LinkFixture, REQ_COM_004_lost_to_connected) {
  EXPECT_EQ(after(1500ms), LinkState::Lost);
  sink.events.clear();
  link.on_valid_packet();
  EXPECT_EQ(link.state(), LinkState::Connected);
  ASSERT_EQ(sink.events.size(), 1U);
  EXPECT_EQ(sink.events[0].from, LinkState::Lost);
  EXPECT_EQ(sink.events[0].to, LinkState::Connected);  // DEGRADED를 거치지 않음
}

// TC-COM-004-02
TEST_F(LinkFixture, REQ_COM_004_transition_events) {
  link.on_valid_packet();  // LOST → CONNECTED (첫 패킷)
  sink.events.clear();
  const auto rx_epoch = clock.epoch_ms();
  clock.advance(500ms);
  link.poll();  // → DEGRADED
  clock.advance(500ms);
  link.poll();  // → LOST
  link.on_valid_packet();  // → CONNECTED

  ASSERT_EQ(sink.events.size(), 3U);
  const LinkState expected[3][2] = {{LinkState::Connected, LinkState::Degraded},
                                    {LinkState::Degraded, LinkState::Lost},
                                    {LinkState::Lost, LinkState::Connected}};
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(sink.events[i].source, Source::Vehicle);
    EXPECT_EQ(sink.events[i].from, expected[i][0]);
    EXPECT_EQ(sink.events[i].to, expected[i][1]);
  }
  EXPECT_EQ(sink.events[0].at_epoch_ms, rx_epoch + 500);
  EXPECT_EQ(sink.events[1].at_epoch_ms, rx_epoch + 1000);
  EXPECT_EQ(sink.events[1].last_rx_epoch_ms, rx_epoch);
}

// TC-COM-004-03
TEST_F(LinkFixture, REQ_COM_004_no_event_without_change) {
  link.on_valid_packet();
  sink.events.clear();
  for (int i = 0; i < 10; ++i) {  // 100ms마다 패킷: 계속 CONNECTED
    clock.advance(100ms);
    link.on_valid_packet();
    link.poll();
  }
  clock.advance(2s);
  link.poll();  // → DEGRADED → LOST가 아니라 바로 LOST 1건 (판정 시점에 이미 2s)
  sink.events.clear();
  for (int i = 0; i < 10; ++i) {  // LOST 지속
    clock.advance(100ms);
    link.poll();
  }
  EXPECT_TRUE(sink.events.empty());
}

// TC-COM-004-04
TEST_F(LinkFixture, REQ_COM_004_no_event_at_startup) {
  link.poll();
  clock.advance(3s);
  link.poll();
  EXPECT_TRUE(sink.events.empty());
}
