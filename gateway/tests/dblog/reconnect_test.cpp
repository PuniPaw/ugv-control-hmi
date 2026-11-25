// DB 재접속 시각: 첫 시도는 바로, 실패마다 간격 두 배(0.5s → 5s 상한), 성공하면 처음 간격으로 (REQ-LOG-003)
#include "ugvc/dblog/reconnect.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "support/fake_clock.hpp"

namespace {

using namespace std::chrono_literals;
using ugvc::dblog::ReconnectSchedule;
using ugvc::test::FakeClock;

}  // namespace

TEST(ReconnectSchedule, REQ_LOG_003_first_attempt_is_immediate_and_retry_waits_500ms) {
  FakeClock clock;
  ReconnectSchedule r(clock);
  EXPECT_TRUE(r.due());
  EXPECT_EQ(r.failed(), 500ms);
  clock.advance(499ms);
  EXPECT_FALSE(r.due());
  clock.advance(1ms);
  EXPECT_TRUE(r.due());
}

TEST(ReconnectSchedule, REQ_LOG_003_retry_interval_doubles_up_to_5s_and_never_stops) {
  FakeClock clock;
  ReconnectSchedule r(clock);
  std::vector<std::chrono::milliseconds> delays;
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(r.due()) << "attempt " << i;
    delays.push_back(r.failed());
    clock.advance(delays.back() - 1ms);
    ASSERT_FALSE(r.due()) << "attempt " << i;
    clock.advance(1ms);
  }
  const std::vector<std::chrono::milliseconds> first{500ms, 1000ms, 2000ms, 4000ms, 5000ms, 5000ms};
  EXPECT_EQ(std::vector<std::chrono::milliseconds>(delays.begin(), delays.begin() + 6), first);
  EXPECT_EQ(delays.back(), 5000ms);
}

TEST(ReconnectSchedule, REQ_LOG_003_success_resets_retry_interval) {
  FakeClock clock;
  ReconnectSchedule r(clock);
  (void)r.failed();
  clock.advance(500ms);
  (void)r.failed();
  clock.advance(1000ms);
  r.succeeded();
  EXPECT_TRUE(r.due());
  EXPECT_EQ(r.failed(), 500ms);  // 끊긴 뒤 첫 재시도는 다시 0.5s 뒤
}

