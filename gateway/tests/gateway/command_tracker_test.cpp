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
using gateway::CommandIgnored;
using gateway::CommandResult;
using gateway::CommandSent;
using gateway::CommandTracker;
using gateway::UnknownAck;

namespace {

gateway::CommandRequest request(const char* req_id, codec::CmdType t = codec::CmdType::Stop) {
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
                          return std::string(r.repeated ? "again " : "result ") + r.req_id + " " +
                                 gateway::name_of(r.result) + " n=" + std::to_string(r.attempts);
                        },
                        [](const UnknownAck& u) { return "unknown id=" + std::to_string(u.cmd_id); },
                        [](const CommandIgnored& i) { return "ignored " + i.req_id + " (" + i.reason + ")"; },
                    },
                    e);
}

struct CommandTrackerTest : ::testing::Test {
  test::FakeClock clock;
  CommandTracker tracker{clock, 1};  // cmd_id를 1부터 (기본은 시각 기반 시작값)

  // 쌓인 사건을 꺼내 한 줄씩 문자열로 (순서 비교용)
  std::vector<std::string> events() {
    const auto taken = tracker.take_events();
    std::vector<std::string> out;
    std::transform(taken.begin(), taken.end(), std::back_inserter(out), show);
    return out;
  }
};

}  // namespace

TEST_F(CommandTrackerTest, REQ_CMD_002_first_send_is_immediate_attempt_1) {
  const auto cmd = tracker.submit({"r1", codec::CmdType::Waypoint, 361240000, -1277660000});
  ASSERT_TRUE(cmd.has_value());
  EXPECT_EQ(*cmd, (codec::Command{1, 0x03, 361240000, -1277660000}));
  EXPECT_EQ(events(), std::vector<std::string>{"sent r1 id=1 #1"});
}

TEST_F(CommandTrackerTest, REQ_CMD_002_resend_at_1000ms_not_before_with_same_cmd_id) {
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

TEST_F(CommandTrackerTest, REQ_CMD_002_no_ack_sends_four_times_then_timeout_at_4s) {
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

TEST_F(CommandTrackerTest, REQ_CMD_002_ack_before_timeout_gives_accepted_with_attempts_and_stops_resending) {
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

TEST_F(CommandTrackerTest, REQ_CMD_002_ack_results_are_mapped) {
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

TEST_F(CommandTrackerTest, REQ_CMD_002_late_ack_after_timeout_is_unknown_and_gives_no_result) {
  const auto cmd = tracker.submit(request("r1"));
  for (int i = 0; i < 4; ++i) {
    clock.advance(1s);
    tracker.poll();
  }
  tracker.take_events();
  tracker.on_ack(ack(cmd->cmd_id));
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=1"});
}

TEST_F(CommandTrackerTest, REQ_CMD_005_duplicate_ack_is_unknown) {
  const auto cmd = tracker.submit(request("r1"));
  tracker.on_ack(ack(cmd->cmd_id));
  tracker.take_events();
  tracker.on_ack(ack(cmd->cmd_id));
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=1"});
}

TEST_F(CommandTrackerTest, REQ_CMD_005_ack_with_unassigned_cmd_id_is_unknown_and_keeps_pending) {
  tracker.submit(request("r1"));
  tracker.take_events();
  tracker.on_ack(ack(777));
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=777"});
  clock.advance(1000ms);
  EXPECT_EQ(tracker.poll().size(), 1u);
}

TEST_F(CommandTrackerTest, REQ_CMD_002_same_req_id_while_pending_is_ignored_and_logged) {
  ASSERT_TRUE(tracker.submit(request("r1")).has_value());
  tracker.take_events();
  EXPECT_FALSE(tracker.submit(request("r1")).has_value());
  EXPECT_FALSE(tracker.submit(request("r1", codec::CmdType::Rtb)).has_value());
  EXPECT_EQ(events(), (std::vector<std::string>{"ignored r1 (req_id pending)",
                                                      "ignored r1 (req_id pending with different command)"}));
  clock.advance(1000ms);
  EXPECT_EQ(tracker.poll().size(), 1u);  // 대기 중인 명령은 하나뿐
}

TEST_F(CommandTrackerTest, REQ_CMD_002_resubmitted_req_id_gets_the_same_result_without_resending) {
  const auto first = tracker.submit(request("r1"));
  tracker.on_ack(ack(first->cmd_id));
  tracker.take_events();
  clock.advance(59s);
  EXPECT_FALSE(tracker.submit(request("r1")).has_value());  // 결과를 놓친 HMI의 재송신: 다시 실행하지 않음
  EXPECT_FALSE(tracker.submit(request("r1", codec::CmdType::Rtb)).has_value());
  EXPECT_EQ(events(), (std::vector<std::string>{"again r1 ACCEPTED n=1",
                                                      "ignored r1 (req_id used by a recent different command)"}));
  clock.advance(1s);  // 결과 후 60s: 잊는다
  const auto later = tracker.submit(request("r1"));
  ASSERT_TRUE(later.has_value());
  EXPECT_NE(later->cmd_id, first->cmd_id);
}

TEST_F(CommandTrackerTest, REQ_CMD_002_recent_results_are_bounded) {
  for (std::size_t i = 0; i <= gateway::kMaxRecentResults; ++i) {
    const auto id = "r" + std::to_string(i);
    const auto cmd = tracker.submit({id, codec::CmdType::Stop, 0, 0});
    tracker.on_ack(ack(cmd->cmd_id));
  }
  tracker.take_events();
  EXPECT_TRUE(tracker.submit(request("r0")).has_value());  // 가장 오래된 것은 잊었다
  EXPECT_FALSE(tracker.submit(request("r1")).has_value());
}

TEST_F(CommandTrackerTest, REQ_CMD_002_invalid_command_is_rejected_unless_its_req_id_is_in_use) {
  tracker.reject("bad");
  const auto cmd = tracker.submit(request("busy"));
  tracker.reject("busy");  // 대기 중인 req_id: 결과를 두 번 내지 않는다
  tracker.on_ack(ack(cmd->cmd_id));
  tracker.reject("busy");  // 올바른 명령의 최근 결과가 있는 req_id
  EXPECT_EQ(events(), (std::vector<std::string>{"result bad REJECTED_INVALID n=0", "sent busy id=1 #1",
                                                      "ignored busy (invalid command with a req_id in use)",
                                                      "result busy ACCEPTED n=1",
                                                      "ignored busy (invalid command with a req_id in use)"}));
}

TEST_F(CommandTrackerTest, REQ_CMD_002_rejected_req_id_is_remembered_so_it_has_one_result) {
  tracker.reject("x");
  EXPECT_FALSE(tracker.submit(request("x")).has_value());  // 고친 명령을 같은 req_id로: 보내지 않는다
  tracker.reject("x");                                     // 같은 형식 오류를 다시: 같은 거절을 다시 알린다
  EXPECT_EQ(events(), (std::vector<std::string>{"result x REJECTED_INVALID n=0",
                                                      "ignored x (req_id used by a recent invalid command)",
                                                      "again x REJECTED_INVALID n=0"}));
  clock.advance(gateway::kRecentResultTtl);  // 60초가 지나면 잊는다
  EXPECT_TRUE(tracker.submit(request("x")).has_value());
}

TEST_F(CommandTrackerTest, REQ_CMD_002_resend_schedule_does_not_drift_with_late_polls) {
  tracker.submit(request("r1"));
  tracker.take_events();
  clock.advance(1040ms);  // 판정 주기 때문에 40ms 늦게 판정
  EXPECT_EQ(tracker.poll().size(), 1u);
  clock.advance(960ms);  // 2000ms: 예정대로 3번째 송신 (늦음이 쌓이지 않는다)
  EXPECT_EQ(tracker.poll().size(), 1u);
  clock.advance(1000ms);  // 3000ms
  EXPECT_EQ(tracker.poll().size(), 1u);
  tracker.take_events();
  clock.advance(1000ms);  // 4000ms: TIMEOUT
  tracker.poll();
  EXPECT_EQ(events(), std::vector<std::string>{"result r1 TIMEOUT n=4"});
}

TEST_F(CommandTrackerTest, REQ_CMD_002_resend_after_a_poll_stall_still_waits_1s_for_ack) {
  tracker.submit(request("r1"));
  tracker.take_events();
  clock.advance(3500ms);  // io 스레드가 3.5s 멈춘 뒤 첫 판정: 밀린 재전송을 몰아 보내지 않는다
  EXPECT_EQ(tracker.poll().size(), 1u);
  clock.advance(50ms);
  EXPECT_TRUE(tracker.poll().empty());
  clock.advance(950ms);  // 4.5s: 늦은 송신 후 1.0s
  EXPECT_EQ(tracker.poll().size(), 1u);
  clock.advance(1000ms);  // 5.5s
  EXPECT_EQ(tracker.poll().size(), 1u);
  tracker.take_events();
  clock.advance(999ms);  // 마지막 송신 뒤 1.0s 전에는 TIMEOUT 아님
  tracker.poll();
  EXPECT_TRUE(tracker.take_events().empty());
  clock.advance(1ms);
  tracker.poll();
  EXPECT_EQ(events(), std::vector<std::string>{"result r1 TIMEOUT n=4"});
}

TEST_F(CommandTrackerTest, REQ_CMD_002_concurrent_commands_have_independent_timers) {
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

TEST_F(CommandTrackerTest, REQ_CMD_005_events_in_order_sent_resent_result) {
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

TEST_F(CommandTrackerTest, REQ_CMD_002_cmd_id_skips_0_on_wrap) {
  test::FakeClock clock;
  CommandTracker t(clock, std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(t.submit(request("a"))->cmd_id, std::numeric_limits<std::uint32_t>::max());
  EXPECT_EQ(t.submit(request("b"))->cmd_id, 1u);

  CommandTracker zero(clock, 0);
  EXPECT_EQ(zero.submit(request("a"))->cmd_id, 1u);
}

TEST_F(CommandTrackerTest, REQ_CMD_002_cmd_id_start_follows_the_clock_so_a_restarted_gw_does_not_reuse_ids) {
  // 0.1초 단위 시각에서 시작: 1초 뒤 다시 시작한 GW는 10만큼 뒤의 번호부터 (ICD 7.3)
  EXPECT_EQ(gateway::initial_cmd_id(1'794'729'600'000ULL), static_cast<std::uint32_t>(17'947'296'000ULL % (1ULL << 32)));
  test::FakeClock first(1'794'729'600'000ULL);
  test::FakeClock restarted(1'794'729'601'000ULL);
  CommandTracker gw1(first);
  CommandTracker gw2(restarted);
  const auto a = gw1.submit(request("a"))->cmd_id;
  for (int i = 0; i < 9; ++i) gw1.submit(request(("x" + std::to_string(i)).c_str()));
  const auto b = gw2.submit(request("b"))->cmd_id;
  EXPECT_EQ(b - a, 10u);  // 앞 GW가 1초 동안 명령 10개를 써도 겹치지 않는다
  EXPECT_EQ(gateway::initial_cmd_id(0), 1u);  // 0은 쓰지 않는다
}

TEST_F(CommandTrackerTest, REQ_CMD_005_ack_with_undefined_result_is_unknown_and_keeps_pending) {
  const auto cmd = tracker.submit(request("r1"));
  tracker.take_events();
  tracker.on_ack({cmd->cmd_id, 9});
  EXPECT_EQ(events(), std::vector<std::string>{"unknown id=1"});
  clock.advance(1000ms);
  EXPECT_EQ(tracker.poll().size(), 1u);
}
