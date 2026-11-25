// DbSink: DB가 멈추거나 없어도 publish()가 막히지 않고, 버린 기록을 원인별로 세며, 재접속을 계속한다 (REQ-LOG-003)
#include "db_sink.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
using ugvc::app::DbSink;
using ugvc::app::DbStats;
using ugvc::app::IDbSession;
using ugvc::dblog::Row;
using ugvc::dblog::Table;
using Clock = std::chrono::steady_clock;

// 시험이 조종하는 가짜 DB. DbSink가 세션을 소유하므로 상태는 shared_ptr로 시험과 나눈다
// (떼어 낸 DB 스레드가 시험 함수보다 오래 살아도 안전하게).
struct FakeDb {
  std::mutex m;
  std::condition_variable cv;
  bool up = true;
  bool hang = false;        // write가 풀릴 때까지 멈춘다 (멈춘 DB 서버)
  bool fail_next_write = false;
  int opens = 0;
  int writes = 0;
  std::vector<Row> written;

  void set(bool FakeDb::*field, bool v) {
    std::lock_guard<std::mutex> lock(m);
    this->*field = v;
    cv.notify_all();
  }
  template <typename F>
  auto read(F f) {
    std::lock_guard<std::mutex> lock(m);
    return f();
  }
};

class FakeSession final : public IDbSession {
 public:
  explicit FakeSession(std::shared_ptr<FakeDb> db) : db_(std::move(db)) {}
  std::optional<std::string> open() override {
    std::lock_guard<std::mutex> lock(db_->m);
    ++db_->opens;
    if (!db_->up) return std::string("connection refused");
    return std::nullopt;
  }
  std::optional<std::string> write(const std::vector<Row>& rows) override {
    std::unique_lock<std::mutex> lock(db_->m);
    ++db_->writes;
    db_->cv.notify_all();
    db_->cv.wait(lock, [this] { return !db_->hang; });
    if (!db_->up || db_->fail_next_write) {
      db_->fail_next_write = false;
      return std::string("server closed the connection");
    }
    db_->written.insert(db_->written.end(), rows.begin(), rows.end());
    return std::nullopt;
  }
  void close() override {}

 private:
  std::shared_ptr<FakeDb> db_;
};

constexpr DbSink::Config kConfig{100, 10, 1ms, 4ms};

Row row(int i) { return Row{Table::Event, {std::to_string(i), std::string("link"), std::nullopt, std::string("{}")}}; }

template <typename Pred>
bool eventually(Pred pred, std::chrono::milliseconds timeout = 2000ms) {
  const auto deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

std::vector<std::string> notices_until(DbSink& sink, const std::string& wanted) {
  std::vector<std::string> all;
  eventually([&] {
    const auto fresh = sink.take_notices();
    all.insert(all.end(), fresh.begin(), fresh.end());
    return std::any_of(all.begin(), all.end(), [&](const std::string& n) { return n.rfind(wanted, 0) == 0; });
  });
  return all;
}

bool connected(DbSink& sink) {
  const auto n = notices_until(sink, "db connected");
  return !n.empty() && n.back() == "db connected";
}

}  // namespace

TEST(DbSink, REQ_LOG_003_writes_records_in_order_when_db_is_up) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(sink));
  for (int i = 0; i < 50; ++i) sink.publish(row(i));
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 50; }));
  db->read([&] {
    for (int i = 0; i < 50; ++i) EXPECT_EQ(db->written[static_cast<std::size_t>(i)].values[0], std::to_string(i));
    return 0;
  });
  EXPECT_EQ(sink.stats().dropped(), 0U);
}

// 저장 중인 DB가 멈춰도 publish()는 즉시 돌아오고, 대기열 상한을 넘는 기록은 queue_full로 센다
TEST(DbSink, REQ_LOG_003_publish_does_not_block_while_db_write_hangs) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(sink));
  db->set(&FakeDb::hang, true);
  sink.publish(row(0));
  ASSERT_TRUE(eventually([&] { return db->read([&] { return db->writes == 1; }); }));  // DB 스레드가 write에 멈춤

  const auto start = Clock::now();
  for (int i = 1; i <= 10'000; ++i) sink.publish(row(i));
  const auto took = Clock::now() - start;
  EXPECT_LT(took, 200ms) << "publish blocked behind a hung DB write";

  DbStats s = sink.stats();
  EXPECT_EQ(s.queue_full, 10'000U - kConfig.max_queued);
  EXPECT_EQ(s.written, 0U);

  db->set(&FakeDb::hang, false);  // DB가 돌아오면 대기열에 남은 기록을 저장한다
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1 + kConfig.max_queued; }));
  s = sink.stats();
  EXPECT_EQ(s.written + s.dropped(), 10'001U);  // 넣은 기록 = 저장 + 버림
}

// DB가 없으면 기록을 버리고 db_down으로 세며, 재접속을 멈추지 않는다. 끊김 알림은 한 번만
TEST(DbSink, REQ_LOG_003_records_while_db_is_down_are_dropped_and_reconnect_continues) {
  auto db = std::make_shared<FakeDb>();
  db->up = false;
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  for (int i = 0; i < 30; ++i) sink.publish(row(i));
  EXPECT_EQ(sink.stats().db_down, 30U);
  ASSERT_TRUE(eventually([&] { return db->read([&] { return db->opens >= 5; }); }));

  std::vector<std::string> notices = sink.take_notices();
  ASSERT_EQ(notices.size(), 1U);
  EXPECT_EQ(notices[0].rfind("db unavailable: connection refused", 0), 0U) << notices[0];

  db->set(&FakeDb::up, true);
  ASSERT_TRUE(connected(sink));
  sink.publish(row(99));
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1; }));
  EXPECT_EQ(sink.stats().db_down, 30U);
}

// 저장이 실패하면 그 배치와 남은 대기열을 버려 원인별로 세고, 다시 접속해 이어서 저장한다
TEST(DbSink, REQ_LOG_003_write_failure_drops_batch_and_reconnects) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(sink));
  db->set(&FakeDb::hang, true);
  sink.publish(row(0));
  ASSERT_TRUE(eventually([&] { return db->read([&] { return db->writes == 1; }); }));
  for (int i = 1; i <= 5; ++i) sink.publish(row(i));  // 실패할 배치 뒤에 대기열 5개
  db->set(&FakeDb::fail_next_write, true);
  db->set(&FakeDb::hang, false);

  const auto notices = notices_until(sink, "db connected");
  ASSERT_GE(notices.size(), 2U);
  EXPECT_EQ(notices[0].rfind("db write failed, dropped 1 records: server closed the connection", 0), 0U)
      << notices[0];
  EXPECT_EQ(notices.back(), "db connected");
  DbStats s = sink.stats();
  EXPECT_EQ(s.write_error, 1U);
  EXPECT_EQ(s.db_down, 5U);
  EXPECT_EQ(db->read([&] { return db->opens; }), 2);

  sink.publish(row(6));
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1; }));
}

// DB가 멈춰 있어도 GW 종료가 막히지 않는다 (소멸자는 최대 0.5s만 기다린다)
TEST(DbSink, REQ_LOG_003_shutdown_does_not_wait_for_a_hung_db) {
  auto db = std::make_shared<FakeDb>();
  auto sink = std::make_unique<DbSink>(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(*sink));
  db->set(&FakeDb::hang, true);
  sink->publish(row(0));
  ASSERT_TRUE(eventually([&] { return db->read([&] { return db->writes == 1; }); }));
  const auto start = Clock::now();
  sink.reset();
  EXPECT_LT(Clock::now() - start, 1500ms);
  db->set(&FakeDb::hang, false);  // 떼어 낸 DB 스레드를 끝낸다
}

