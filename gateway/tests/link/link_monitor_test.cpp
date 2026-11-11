#include "ugvc/link/link_monitor.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "support/fake_clock.hpp"

using namespace std::chrono_literals;
using namespace ugvc::link;
using ugvc::test::FakeClock;

namespace {

// LinkMonitor가 반환한 전이를 events에 모으는 공통 준비
struct LinkFixture : ::testing::Test {
  FakeClock clock;
  LinkMonitor link{clock};
  std::vector<LinkChange> events;

  void packet() { record(link.on_valid_packet()); }
  void poll() { record(link.poll()); }
  void record(std::optional<LinkChange> c) {
    if (c) events.push_back(*c);
  }

  LinkState after(std::chrono::milliseconds elapsed) {
    packet();
    clock.advance(elapsed);
    poll();
    return link.state();
  }
};

}  // namespace

// TC-COM-003-01
TEST_F(LinkFixture, REQ_COM_003_initially_lost) {
  EXPECT_EQ(link.state(), LinkState::Lost);
  clock.advance(5s);
  poll();
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
  events.clear();
  packet();
  EXPECT_EQ(link.state(), LinkState::Connected);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].from, LinkState::Lost);
  EXPECT_EQ(events[0].to, LinkState::Connected);  // DEGRADED를 거치지 않음
}

// TC-COM-004-02
TEST_F(LinkFixture, REQ_COM_004_transition_events) {
  packet();  // LOST → CONNECTED (첫 패킷)
  events.clear();
  const auto rx_epoch = clock.epoch_ms();
  clock.advance(500ms);
  poll();  // → DEGRADED
  clock.advance(500ms);
  poll();  // → LOST
  packet();  // → CONNECTED

  ASSERT_EQ(events.size(), 3U);
  const LinkState expected[3][2] = {{LinkState::Connected, LinkState::Degraded},
                                    {LinkState::Degraded, LinkState::Lost},
                                    {LinkState::Lost, LinkState::Connected}};
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(events[i].from, expected[i][0]);
    EXPECT_EQ(events[i].to, expected[i][1]);
  }
  EXPECT_EQ(events[0].at_epoch_ms, rx_epoch + 500);
  EXPECT_EQ(events[1].at_epoch_ms, rx_epoch + 1000);
  EXPECT_EQ(events[1].last_rx_epoch_ms, rx_epoch);
}

// TC-COM-004-03
TEST_F(LinkFixture, REQ_COM_004_no_event_without_change) {
  packet();
  events.clear();
  for (int i = 0; i < 10; ++i) {  // 100ms마다 패킷: 계속 CONNECTED
    clock.advance(100ms);
    packet();
    poll();
  }
  clock.advance(2s);
  poll();  // → DEGRADED → LOST가 아니라 바로 LOST 1건 (판정 시점에 이미 2s)
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events[0].to, LinkState::Lost);
  events.clear();
  for (int i = 0; i < 10; ++i) {  // LOST 지속
    clock.advance(100ms);
    poll();
  }
  EXPECT_TRUE(events.empty());
}

// TC-COM-004-04
TEST_F(LinkFixture, REQ_COM_004_no_event_at_startup) {
  poll();
  clock.advance(3s);
  poll();
  EXPECT_TRUE(events.empty());
}
