#include "ugvc/ingress/seq_tracker.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <vector>

using namespace ugvc::ingress;

namespace {

// seq 열을 차례로 넣고 각 결과를 모은다
std::vector<SeqTracker::Result> feed(SeqTracker& t, std::initializer_list<std::uint32_t> seqs) {
  std::vector<SeqTracker::Result> out;
  std::transform(seqs.begin(), seqs.end(), std::back_inserter(out),
                 [&](std::uint32_t s) { return t.observe(s); });
  return out;
}

std::uint32_t total_lost(const std::vector<SeqTracker::Result>& rs) {
  return std::accumulate(rs.begin(), rs.end(), std::uint32_t{0},
                         [](std::uint32_t sum, const SeqTracker::Result& r) { return sum + r.lost; });
}

}  // namespace

// TC-COM-005-01
TEST(SeqTracker, REQ_COM_005_contiguous_no_loss) {
  SeqTracker t;
  const auto rs = feed(t, {10, 11, 12});
  for (const auto& r : rs) EXPECT_TRUE(r.in_order);
  EXPECT_EQ(total_lost(rs), 0U);
}

// TC-COM-005-02
TEST(SeqTracker, REQ_COM_005_gap_counts_loss) {
  SeqTracker t;
  const auto rs = feed(t, {10, 13});
  EXPECT_TRUE(rs[1].in_order);
  EXPECT_EQ(rs[1].lost, 2U);
}

// TC-COM-005-03
TEST(SeqTracker, REQ_COM_005_wraparound) {
  SeqTracker a;
  EXPECT_EQ(total_lost(feed(a, {0xFFFF'FFFFU, 0U})), 0U);
  SeqTracker b;
  const auto rs = feed(b, {0xFFFF'FFFEU, 1U});
  EXPECT_TRUE(rs[1].in_order);
  EXPECT_EQ(rs[1].lost, 2U);
}

// TC-COM-006-01
TEST(SeqTracker, REQ_COM_006_older_rejected) {
  SeqTracker t;
  const auto rs = feed(t, {10, 9});
  EXPECT_FALSE(rs[1].in_order);
}

// TC-COM-006-02
TEST(SeqTracker, REQ_COM_006_duplicate_rejected) {
  SeqTracker t;
  const auto rs = feed(t, {10, 10});
  EXPECT_FALSE(rs[1].in_order);
}

// TC-COM-006-03: 차이 2^31 − 1은 "큼", 정확히 2^31은 "크지 않음"
TEST(SeqTracker, REQ_COM_006_rfc1982_boundary) {
  EXPECT_TRUE(seq_after(0x7FFF'FFFFU, 0U));
  EXPECT_FALSE(seq_after(0x8000'0000U, 0U));
  SeqTracker t;
  const auto rs = feed(t, {0U, 0x8000'0000U});
  EXPECT_FALSE(rs[1].in_order);
}

// TC-COM-006-04: 늦게 도착한 패킷은 폐기, 이미 센 유실 1은 그대로
TEST(SeqTracker, REQ_COM_006_late_arrival_keeps_loss) {
  SeqTracker t;
  const auto rs = feed(t, {10, 12, 11, 13});
  EXPECT_EQ(rs[1].lost, 1U);
  EXPECT_FALSE(rs[2].in_order);
  EXPECT_TRUE(rs[3].in_order);
  EXPECT_EQ(total_lost(rs), 1U);
}

// TC-COM-006-05
TEST(SeqTracker, REQ_COM_006_first_packet_is_base) {
  SeqTracker t;
  const auto rs = feed(t, {123'456U, 123'457U});
  EXPECT_TRUE(rs[0].in_order);
  EXPECT_EQ(total_lost(rs), 0U);
}

// TC-COM-005-09의 SeqTracker 부분: rebase 후 첫 seq는 비교 없이 기준값 (작아져도 수락, 유실 0)
TEST(SeqTracker, REQ_COM_005_rebase_accepts_any_seq) {
  SeqTracker t;
  feed(t, {5000, 5001});
  t.rebase();
  const auto rs = feed(t, {0, 1});
  EXPECT_TRUE(rs[0].in_order);
  EXPECT_EQ(total_lost(rs), 0U);
}
