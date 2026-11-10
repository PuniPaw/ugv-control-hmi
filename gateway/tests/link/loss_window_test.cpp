#include "ugvc/link/loss_window.hpp"

#include <gtest/gtest.h>

#include "support/fake_clock.hpp"

using namespace std::chrono_literals;
using ugvc::link::LossWindow;
using ugvc::test::FakeClock;

// TC-COM-005-04: 10초 안에 수신 90, 유실 10 → 0.1
TEST(LossRate, REQ_COM_005_formula) {
  FakeClock clock;
  LossWindow w;
  for (int i = 0; i < 9; ++i) {
    w.record(clock.now(), 10, 1);
    clock.advance(1s);
  }
  w.record(clock.now(), 0, 1);
  ASSERT_TRUE(w.rate(clock.now()).has_value());
  EXPECT_DOUBLE_EQ(*w.rate(clock.now()), 10.0 / 100.0);
}

// TC-COM-005-05: 10초가 지난 버킷은 빠진다
TEST(LossRate, REQ_COM_005_window_10s) {
  FakeClock clock;
  LossWindow w;
  w.record(clock.now(), 0, 5);  // t=0: 유실 5
  clock.advance(1s);
  w.record(clock.now(), 10, 0);  // t=1: 수신 10
  clock.advance(9s);             // t=10: t=0 버킷은 창 밖, t=1은 창 안
  EXPECT_DOUBLE_EQ(*w.rate(clock.now()), 0.0);
  clock.advance(1s);  // t=11: 둘 다 창 밖
  EXPECT_FALSE(w.rate(clock.now()).has_value());
}

// TC-COM-005-06
TEST(LossRate, REQ_COM_005_empty_is_null) {
  FakeClock clock;
  LossWindow w;
  EXPECT_FALSE(w.rate(clock.now()).has_value());
}

// 버킷 자리 재사용: 10초 뒤 같은 자리에 쓰면 예전 값은 지워진다
TEST(LossRate, REQ_COM_005_slot_reuse_clears_old_counts) {
  FakeClock clock;
  LossWindow w;
  w.record(clock.now(), 0, 100);
  clock.advance(10s);
  w.record(clock.now(), 1, 0);
  EXPECT_DOUBLE_EQ(*w.rate(clock.now()), 0.0);
}
