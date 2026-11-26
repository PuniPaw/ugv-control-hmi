// DbSink: DB가 멈추거나 없어도 publish()가 막히지 않고, 버린 기록을 원인별로 세며, 재접속을 계속한다 (REQ-LOG-003).
//
// 실제 DB 스레드를 쓰는 시험이다 (STP 2.2 예외). 시간 상한으로 판정하지 않는다: "막히지 않음"은 가짜 DB가
// 멈춘 채로 호출이 끝나는 것으로, "바로 다시 접속"은 재접속 간격을 1시간으로 둬도 접속하는 것으로 본다.
// 기다림(eventually)은 DB 스레드의 결과가 보일 때까지의 상한일 뿐 판정 기준이 아니다.
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
using ugvc::app::DbNotice;
using ugvc::app::DbSink;
using ugvc::app::DbStats;
using ugvc::app::IDbSession;
using ugvc::dblog::Row;
using ugvc::dblog::Table;
using Kind = DbNotice::Kind;

// 시험이 조종하는 가짜 DB. DbSink가 세션을 소유하므로 상태는 shared_ptr로 시험과 나눈다
// (떼어 낸 DB 스레드가 시험 함수보다 오래 살아도 안전하게).
struct FakeDb {
  std::mutex m;
  std::condition_variable cv;
  bool up = true;
  std::string refuse_reason = "connection refused";
  bool hang = false;    // write가 풀릴 때까지 멈춘다 (멈춘 DB 서버)
  bool broken = false;  // 다시 접속할 때까지 모든 write가 실패한다 (예: 테이블 삭제. open()이 다시 만든다)
  int opens = 0;
  int writes = 0;
  std::vector<std::string> written;  // 저장한 행의 첫 값

  template <typename F>
  auto with(F f) {
    std::lock_guard<std::mutex> lock(m);
    cv.notify_all();
    return f();
  }
};

class FakeSession final : public IDbSession {
 public:
  explicit FakeSession(std::shared_ptr<FakeDb> db) : db_(std::move(db)) {}
  std::optional<std::string> open() override {
    std::lock_guard<std::mutex> lock(db_->m);
    ++db_->opens;
    if (!db_->up) return db_->refuse_reason;
    db_->broken = false;
    return std::nullopt;
  }
  // 첫 값이 "bad"인 행은 늘 실패한다 (예: 서버 인코딩으로 바꿀 수 없는 문자). "throw"는 예외를 던진다
  std::optional<std::string> write(const Row& row) override {
    std::unique_lock<std::mutex> lock(db_->m);
    ++db_->writes;
    db_->cv.notify_all();
    db_->cv.wait(lock, [this] { return !db_->hang; });
    const std::string first = row.values.at(0).value_or("");
    if (first == "throw") throw std::bad_alloc();
    if (db_->broken) return std::string("relation \"telemetry\" does not exist");
    if (first == "bad") return std::string("invalid byte sequence");
    db_->written.push_back(first);
    return std::nullopt;
  }
  void close() override {}

 private:
  std::shared_ptr<FakeDb> db_;
};

constexpr DbSink::Config kConfig{100, 1ms, 4ms};

Row row(const std::string& first) { return Row{Table::Event, {first, std::string("link"), std::nullopt, std::string("{}")}}; }
Row row(int i) { return row(std::to_string(i)); }

template <typename Pred>
bool eventually(Pred pred) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

// kind 알림이 올 때까지 모은 알림 전부
std::vector<DbNotice> notices_until(DbSink& sink, Kind kind, std::size_t count = 1) {
  std::vector<DbNotice> all;
  eventually([&] {
    const auto fresh = sink.take_notices();
    all.insert(all.end(), fresh.begin(), fresh.end());
    return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), [&](const DbNotice& n) {
             return n.kind == kind;
           })) >= count;
  });
  return all;
}

std::vector<Kind> kinds(const std::vector<DbNotice>& ns) {
  std::vector<Kind> out;
  std::transform(ns.begin(), ns.end(), std::back_inserter(out), [](const DbNotice& n) { return n.kind; });
  return out;
}

bool connected(DbSink& sink) { return kinds(notices_until(sink, Kind::Connected)) == std::vector<Kind>{Kind::Connected}; }

// DB 스레드가 write 안에서 멈출 때까지 기다린다
bool writer_parked(FakeDb& db, int writes) {
  return eventually([&] { return db.with([&] { return db.writes == writes; }); });
}

}  // namespace

TEST(DbSink, REQ_LOG_001_writes_records_in_order_when_db_is_up) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(sink));
  std::vector<std::string> expected;
  for (int i = 0; i < 50; ++i) {
    sink.publish(row(i));
    expected.push_back(std::to_string(i));
  }
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 50; }));
  EXPECT_EQ(db->with([&] { return db->written; }), expected);
  EXPECT_EQ(sink.stats().dropped(), 0U);
}

// 저장 중인 DB가 멈춰 있는 동안 publish 1만 번이 끝난다(막히지 않음). 상한을 넘는 기록은 queue_full
TEST(DbSink, REQ_LOG_003_publish_does_not_block_while_db_write_hangs) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(sink));
  db->with([&] { return db->hang = true; });
  sink.publish(row(0));
  ASSERT_TRUE(writer_parked(*db, 1));

  for (int i = 1; i <= 10'000; ++i) sink.publish(row(i));

  EXPECT_EQ(db->with([&] { return db->writes; }), 1) << "DB가 아직 멈춰 있어야 한다";
  DbStats s = sink.stats();
  EXPECT_EQ(s.queue_full, 10'000U - kConfig.max_queued);
  EXPECT_EQ(s.written, 0U);

  db->with([&] { return db->hang = false; });  // DB가 돌아오면 대기열에 남은 기록을 저장한다
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1 + kConfig.max_queued; }));
  s = sink.stats();
  EXPECT_EQ(s.written + s.dropped(), 10'001U);  // 넣은 기록 = 저장 + 버림
}

// DB가 없으면 기록을 버리고 db_down으로 세며, 재접속을 멈추지 않는다. 같은 사유는 한 번만 알린다
TEST(DbSink, REQ_LOG_003_records_while_db_is_down_are_dropped_and_reconnect_continues) {
  auto db = std::make_shared<FakeDb>();
  db->up = false;
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  for (int i = 0; i < 30; ++i) sink.publish(row(i));
  EXPECT_EQ(sink.stats().db_down, 30U);
  ASSERT_TRUE(eventually([&] { return db->with([&] { return db->opens >= 5; }); }));

  const auto notices = sink.take_notices();
  ASSERT_EQ(kinds(notices), std::vector<Kind>{Kind::ConnectFailed});
  EXPECT_EQ(notices[0].reason, "connection refused");

  db->with([&] { return db->up = true; });
  ASSERT_TRUE(connected(sink));
  sink.publish(row(99));
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1; }));
  EXPECT_EQ(sink.stats().db_down, 30U);
}

// 접속 실패 사유가 바뀌면(예: 거절 → 인증 실패) 다시 알린다
TEST(DbSink, REQ_LOG_003_changed_connect_failure_reason_is_reported_again) {
  auto db = std::make_shared<FakeDb>();
  db->up = false;
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_EQ(kinds(notices_until(sink, Kind::ConnectFailed)), std::vector<Kind>{Kind::ConnectFailed});
  db->with([&] { return db->refuse_reason = "password authentication failed"; });
  const auto notices = notices_until(sink, Kind::ConnectFailed);
  ASSERT_EQ(kinds(notices), std::vector<Kind>{Kind::ConnectFailed});
  EXPECT_EQ(notices[0].reason, "password authentication failed");
}

// 저장이 한 번 실패하면 그 행만 write_error로 세고, 대기열은 그대로 둔 채 바로 다시 접속해 이어 저장한다.
// 다시 접속하면 open()이 DB 상태(예: 삭제된 테이블)를 다시 만든다. 재접속 간격을 1시간으로 둬도 바로 다시 접속해야 한다
TEST(DbSink, REQ_LOG_003_write_failure_drops_only_that_record_and_reconnects_at_once) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), DbSink::Config{100, 1h, 1h});
  ASSERT_TRUE(connected(sink));
  sink.publish(row("first"));  // 이 연결로 한 번 저장한다
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1; }));
  db->with([&] { return db->hang = true; });
  sink.publish(row("a"));
  ASSERT_TRUE(writer_parked(*db, 2));
  for (const char* v : {"b", "c", "d"}) sink.publish(row(v));  // 실패할 행 뒤의 대기열
  db->with([&] {
    db->broken = true;  // 테이블이 지워졌다: 다시 접속할 때까지 저장이 실패한다
    return db->hang = false;
  });

  ASSERT_TRUE(eventually([&] { return sink.stats().written == 4; }));
  EXPECT_EQ(db->with([&] { return db->written; }), (std::vector<std::string>{"first", "b", "c", "d"}));
  const DbStats s = sink.stats();
  EXPECT_EQ(s.write_error, 1U);
  EXPECT_EQ(s.db_down + s.queue_full, 0U);
  const auto notices = sink.take_notices();
  ASSERT_EQ(kinds(notices), (std::vector<Kind>{Kind::WriteFailed, Kind::Connected}));
  EXPECT_EQ(notices[0].reason, "relation \"telemetry\" does not exist");
  EXPECT_EQ(notices[0].retry_in, 0ms);
}

// 다시 접속한 뒤 한 번도 저장하지 못하고 또 실패하면 재접속 간격을 둔다(접속 직후마다 끊는 서버, 계속 실패하는 행).
// 그동안 들어온 기록은 버리지 않고 대기열에 쌓되, 상한을 넘으면 queue_full
TEST(DbSink, REQ_LOG_003_failure_before_any_write_on_a_connection_backs_off_and_keeps_queueing) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), DbSink::Config{100, 1h, 1h});
  ASSERT_TRUE(connected(sink));
  sink.publish(row("bad"));  // 이 연결의 첫 저장이 실패한다
  const auto notices = notices_until(sink, Kind::WriteFailed);
  ASSERT_EQ(kinds(notices), std::vector<Kind>{Kind::WriteFailed});
  EXPECT_EQ(notices[0].retry_in, 1h);  // 간격을 두고 다시 접속한다

  for (int i = 0; i < 150; ++i) sink.publish(row(i));
  const DbStats s = sink.stats();
  EXPECT_EQ(s.write_error, 1U);
  EXPECT_EQ(s.queue_full, 50U);  // 상한 100을 넘은 만큼
  EXPECT_EQ(s.db_down, 0U);      // 연결을 잃은 것이 아니라 다시 접속을 기다리는 중
}

// DB가 멈춰 있어도 close()는 끝나고(GW 종료가 막히지 않음), 저장 중이던 행과 대기열, 그 뒤 기록은 db_down으로
// 한 번씩만 센다 (넣은 기록 = 저장 + 버림)
TEST(DbSink, REQ_LOG_003_close_does_not_wait_for_a_hung_db_and_counts_what_it_leaves) {
  auto db = std::make_shared<FakeDb>();
  auto sink = std::make_unique<DbSink>(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(*sink));
  db->with([&] { return db->hang = true; });
  sink->publish(row(0));
  ASSERT_TRUE(writer_parked(*db, 1));
  sink->publish(row(1));
  sink->publish(row(2));

  sink->close();

  EXPECT_EQ(db->with([&] { return db->writes; }), 1) << "DB가 아직 멈춰 있어야 한다";
  EXPECT_EQ(sink->stats().db_down, 3U);  // 저장 중이던 행 + 대기열 2
  sink->publish(row(3));
  EXPECT_EQ(sink->stats().db_down, 4U);
  sink->close();  // 두 번째 호출은 바로 돌아온다

  db->with([&] { return db->hang = false; });  // 떼어 낸 DB 스레드가 행 0을 마저 저장한다
  ASSERT_TRUE(eventually([&] { return db->with([&] { return db->written.size() == 1; }); }));
  const DbStats s = sink->stats();
  EXPECT_EQ(s.written + s.dropped(), 4U) << "행 0을 저장과 버림에 두 번 세지 않는다";
  EXPECT_EQ(s.db_down, 4U);
}

// DB 스레드에서 예외가 나도 GW는 내려가지 않는다. 저장만 멈추고(Stopped), 저장 중이던 행과 이후 기록은 db_down
TEST(DbSink, REQ_LOG_003_exception_in_db_thread_stops_saving_without_crashing) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), kConfig);
  ASSERT_TRUE(connected(sink));
  sink.publish(row("throw"));
  const auto notices = notices_until(sink, Kind::Stopped);
  ASSERT_EQ(kinds(notices), std::vector<Kind>{Kind::Stopped});
  EXPECT_EQ(sink.stats().db_down, 1U);
  sink.publish(row(1));
  EXPECT_EQ(sink.stats().db_down, 2U);
}

// 저장이 실패한 뒤 다시 접속에도 실패하면 Down이 되고, 그때 대기열은 db_down으로 센다 (SRS v0.12 REQ-LOG-003)
TEST(DbSink, REQ_LOG_003_failed_reconnect_after_a_write_failure_drops_the_queue_as_db_down) {
  auto db = std::make_shared<FakeDb>();
  DbSink sink(std::make_unique<FakeSession>(db), DbSink::Config{100, 1h, 1h});
  ASSERT_TRUE(connected(sink));
  sink.publish(row("first"));  // 한 번 저장한 연결: 저장이 실패하면 바로 다시 접속한다
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1; }));
  db->with([&] { return db->hang = true; });
  sink.publish(row("a"));
  ASSERT_TRUE(writer_parked(*db, 2));
  sink.publish(row("b"));
  sink.publish(row("c"));
  db->with([&] {
    db->broken = true;
    db->up = false;  // 다시 접속도 실패한다
    return db->hang = false;
  });

  const auto notices = notices_until(sink, Kind::ConnectFailed);
  ASSERT_EQ(kinds(notices), (std::vector<Kind>{Kind::WriteFailed, Kind::ConnectFailed}));
  const DbStats s = sink.stats();
  EXPECT_EQ(s.write_error, 1U);
  EXPECT_EQ(s.db_down, 2U);  // 다시 접속에 실패한 순간의 대기열
  sink.publish(row("d"));    // Down이면 들어온 기록은 바로 db_down
  EXPECT_EQ(sink.stats().db_down, 3U);
}

// 재접속 간격은 접속 성공이 아니라 그 연결의 첫 저장 성공에서 처음 값으로 돌아온다.
// 접속 실패로 간격이 4ms까지 늘어도, 저장한 뒤의 "연결 직후 실패"는 다시 첫 간격(1ms)부터
TEST(DbSink, REQ_LOG_003_first_successful_write_resets_the_reconnect_interval) {
  auto db = std::make_shared<FakeDb>();
  db->up = false;
  DbSink sink(std::make_unique<FakeSession>(db), DbSink::Config{100, 1ms, 4ms});
  ASSERT_TRUE(eventually([&] { return db->with([&] { return db->opens >= 4; }); }));  // 간격 1·2·4·4ms
  db->with([&] { return db->up = true; });
  ASSERT_EQ(kinds(notices_until(sink, Kind::Connected)), (std::vector<Kind>{Kind::ConnectFailed, Kind::Connected}));
  sink.publish(row("ok"));
  ASSERT_TRUE(eventually([&] { return sink.stats().written == 1; }));

  db->with([&] { return db->hang = true; });
  sink.publish(row("x"));  // 저장한 연결에서 실패 → 바로 다시 접속(테이블을 다시 만듦)
  ASSERT_TRUE(writer_parked(*db, 2));
  sink.publish(row("bad"));  // 다시 접속한 연결의 첫 저장이 실패 → 간격
  db->with([&] {
    db->broken = true;
    return db->hang = false;
  });

  const auto notices = notices_until(sink, Kind::WriteFailed, 2);
  std::vector<DbNotice> failed;
  std::copy_if(notices.begin(), notices.end(), std::back_inserter(failed),
               [](const DbNotice& n) { return n.kind == Kind::WriteFailed; });
  ASSERT_EQ(failed.size(), 2U);
  EXPECT_EQ(failed[0].retry_in, 0ms);  // 저장한 연결: 바로
  EXPECT_EQ(failed[1].retry_in, 1ms);  // 첫 저장 성공에서 되돌린 첫 간격 (접속 실패의 4ms가 아님)
}
