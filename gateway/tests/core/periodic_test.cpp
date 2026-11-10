#include "ugvc/core/periodic.hpp"

#include <gtest/gtest.h>

#include "support/fake_clock.hpp"

using namespace std::chrono_literals;
using namespace ugvc;
using test::FakeClock;

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
