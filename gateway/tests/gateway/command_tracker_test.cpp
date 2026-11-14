#include "ugvc/gateway/command_tracker.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>

#include "support/fake_clock.hpp"
#include "ugvc/core/overloaded.hpp"

using namespace ugvc;
using namespace std::chrono_literals;
using gateway::CmdResult;
using gateway::CommandEvent;
using gateway::CommandResult;
using gateway::CommandSent;
using gateway::CommandTracker;
using gateway::UnknownAck;

namespace {

if03::CommandRequest request(const char* req_id, codec::CmdType t = codec::CmdType::Stop) {
  return {req_id, t, 0, 0};
}

codec::Ack ack(std::uint32_t id, codec::AckResult r = codec::AckResult::Accepted) {
  return {id, static_cast<std::uint8_t>(r)};
}

// 사건을 한 줄 문자열로 만들어 순서까지 비교한다
std::string show(const CommandEvent& e) {
  return std::visit(core::overloaded{
                        [](const CommandSent& s) {
                          return "sent " + s.req_id + " id=" + std::to_string(s.cmd_id) + " #" + std::to_string(s.attempt);
                        },
                        [](const CommandResult& r) {
                          return "result " + r.req_id + " " + gateway::name_of(r.result) + " n=" + std::to_string(r.attempts);
                        },
                        [](const UnknownAck& u) { return "unknown id=" + std::to_string(u.cmd_id); },
                    },
                    e);
}

struct Fixture : ::testing::Test {
  test::FakeClock clock;
  CommandTracker tracker{clock};

  // 쌓인 사건을 꺼내 한 줄씩 문자열로 (순서 비교용)
  std::vector<std::string> events() {
    const auto taken = tracker.take_events();
    std::vector<std::string> out;
    std::transform(taken.begin(), taken.end(), std::back_inserter(out), show);
    return out;
  }
};

}  // namespace

TEST_F(Fixture, REQ_CMD_002_first_send_is_immediate_attempt_1) {
  const auto cmd = tracker.submit({"r1", codec::CmdType::Waypoint, 361240000, -1277660000});
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(*cmd, (codec::Command{1, 0x03, 361240000, -1277660000}));
  EXPECT_EQ(events(), std::vector<std::string>{"sent r1 id=1 #1"});
}

TEST_F(Fixture, REQ_CMD_002_resend_at_1000ms_not_before_with_same_cmd_id) {
  const auto first = tracker.submit(request("r1"));
  tracker.take_events();
  clock.advance(999ms);
  EXPECT_TRUE(tracker.poll().empty());
  clock.advance(1ms);
  const auto again = tracker.poll();
  ASSERT_EQ(again.size(), 1u);
  EXPECT_EQ(again[0], *first);
  EXPECT_EQ(events(), std::vector<std::string>{"sent r1 id=1 #2"});
}

TEST_F(Fixture, REQ_CMD_002_no_ack_sends_four_times_then_timeout_at_4s) {
  tracker.submit(request("r1"));
  int resends = 0;
  for (int i = 0; i < 3; ++i) {
    clock.advance(999ms);
    EXPECT_TRUE(tracker.poll().empty()) << "t=" << i;
    clock.advance(1ms);
    resends += static_cast<int>(tracker.poll().size());
  }
  EXPECT_EQ(resends, 3);
  tracker.take_events();

  clock.advance(999ms);  // 3999ms
  EXPECT_TRUE(tracker.poll().empty());
  EXPECT_TRUE(tracker.take_events().empty());
  clock.advance(1ms);  // 4000ms
  EXPECT_TRUE(tracker.poll().empty());  // 5번째 송신은 없다
  EXPECT_EQ(events(), std::vector<std::string>{"result r1 TIMEOUT n=4"});
  clock.advance(10s);
  EXPECT_TRUE(tracker.poll().empty());
  EXPECT_TRUE(tracker.take_events().empty());
}

TEST_F(Fixture, REQ_CMD_002_ack_before_timeout_gives_accepted_with_attempts_and_stops_resending) {
  const auto cmd = tracker.submit(request("r1"));
  clock.advance(1000ms);
  tracker.poll();
  tracker.take_events();
  tracker.on_ack(ack(cmd->cmd_id));
  EXPECT_EQ(events(), std::vector<std::string>{"result r1 ACCEPTED n=2"});
  clock.advance(10s);
  EXPECT_TRUE(tracker.poll().empty());
  EXPECT_TRUE(tracker.take_events().empty());
}

TEST_F(Fixture, REQ_CMD_002_ack_results_are_mapped) {
  const auto a = tracker.submit(request("a"));
  const auto b = tracker.submit(request("b"));
  const auto c = tracker.submit(request("c"));
  tracker.take_events();
  tracker.on_ack(ack(a->cmd_id, codec::AckResult::Accepted));
  tracker.on_ack(ack(b->cmd_id, codec::AckResult::RejectedInvalid));
  tracker.on_ack(ack(c->cmd_id, codec::AckResult::RejectedState));
  EXPECT_EQ(events(), (std::vector<std::string>{"result a ACCEPTED n=1", "result b REJECTED_INVALID n=1",
                                                      "result c REJECTED_STATE n=1"}));
}

TEST_F(Fixture, REQ_CMD_002_late_ack_after_timeout_is_unknown_and_gives_no_result) {
  const auto cmd = tracker.submit(request("r1"));
  for (int i = 0; i < 4; ++i) {
    clock.advance(1s);
    tracker.poll();
  }
  tracker.take_events();
  tracker.on_ack(ack(cmd->cmd_id));
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=1"});
}

TEST_F(Fixture, REQ_CMD_005_duplicate_ack_is_unknown) {
  const auto cmd = tracker.submit(request("r1"));
  tracker.on_ack(ack(cmd->cmd_id));
  tracker.take_events();
  tracker.on_ack(ack(cmd->cmd_id));
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=1"});
}

TEST_F(Fixture, REQ_CMD_005_ack_with_unassigned_cmd_id_is_unknown_and_keeps_pending) {
  tracker.submit(request("r1"));
  tracker.take_events();
  tracker.on_ack(ack(777));
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=777"});
  clock.advance(1000ms);
  EXPECT_EQ(tracker.poll().size(), 1u);
}

TEST_F(Fixture, REQ_CMD_002_same_req_id_while_pending_is_ignored) {
  ASSERT_TRUE(tracker.submit(request("r1")).has_value());
  tracker.take_events();
  EXPECT_FALSE(tracker.submit(request("r1", codec::CmdType::Rtb)).has_value());
  EXPECT_TRUE(tracker.take_events().empty());
  clock.advance(1000ms);
  EXPECT_EQ(tracker.poll().size(), 1u);  // 대기 중인 명령은 하나뿐
}

TEST_F(Fixture, REQ_CMD_002_req_id_can_be_reused_after_result) {
  const auto first = tracker.submit(request("r1"));
  tracker.on_ack(ack(first->cmd_id));
  const auto second = tracker.submit(request("r1"));
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(second->cmd_id, first->cmd_id);
}

TEST_F(Fixture, REQ_CMD_002_concurrent_commands_have_independent_timers) {
  const auto a = tracker.submit(request("a"));
  clock.advance(600ms);
  const auto b = tracker.submit(request("b"));
  ASSERT_NE(a->cmd_id, b->cmd_id);
  tracker.take_events();

  clock.advance(400ms);  // a 1000ms, b 400ms
  const auto first = tracker.poll();
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first[0].cmd_id, a->cmd_id);

  tracker.on_ack(ack(a->cmd_id));
  clock.advance(600ms);  // b 1000ms
  const auto second = tracker.poll();
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].cmd_id, b->cmd_id);
  EXPECT_EQ(events(), (std::vector<std::string>{"sent a id=1 #2", "result a ACCEPTED n=2", "sent b id=2 #2"}));
}

TEST_F(Fixture, REQ_CMD_005_events_in_order_sent_resent_result) {
  const auto cmd = tracker.submit(request("r1"));
  clock.advance(1000ms);
  tracker.poll();
  clock.advance(1000ms);
  tracker.poll();
  tracker.on_ack(ack(cmd->cmd_id));
  EXPECT_EQ(events(), (std::vector<std::string>{"sent r1 id=1 #1", "sent r1 id=1 #2", "sent r1 id=1 #3",
                                                      "result r1 ACCEPTED n=3"}));
  EXPECT_TRUE(tracker.take_events().empty());  // 꺼내면 비워진다
}

TEST(CommandTracker, REQ_CMD_002_cmd_id_starts_at_1_and_skips_0_on_wrap) {
  test::FakeClock clock;
  CommandTracker t(clock, std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(t.submit(request("a"))->cmd_id, std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(t.submit(request("b"))->cmd_id, 1u);

  CommandTracker fresh(clock);
  EXPECT_EQ(fresh.submit(request("a"))->cmd_id, 1u);
  EXPECT_EQ(fresh.submit(request("b"))->cmd_id, 2u);
  CommandTracker zero(clock, 0);
  EXPECT_EQ(zero.submit(request("a"))->cmd_id, 1u);
}

TEST_F(Fixture, REQ_CMD_005_ack_with_undefined_result_is_unknown_and_keeps_pending) {
  const auto cmd = tracker.submit(request("r1"));
  tracker.take_events();
  tracker.on_ack({cmd->cmd_id, 9});
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=1"});
  clock.advance(1000ms);
  EXPECT_EQ(tracker.poll().size(), 1u);
}
