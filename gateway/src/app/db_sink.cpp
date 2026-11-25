#include "db_sink.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>

#include "ugvc/core/clock.hpp"

namespace ugvc::app {

namespace {

constexpr auto kFlushTimeout = std::chrono::milliseconds(500);
constexpr std::size_t kMaxNotices = 16;  // io 스레드가 1Hz로 꺼낸다. 접속 상태가 바뀔 때만 생긴다

}  // namespace

// 잠금 규칙: session을 뺀 필드는 모두 mutex로 보호한다. session은 DB 스레드만 쓰고, 그 호출(블로킹)은
// 잠금을 놓고 한다. 그래서 DB 호출이 오래 걸려도 publish()는 기다리지 않는다.
struct DbSink::State {
  State(std::unique_ptr<IDbSession> s, const Config& c)
      : session(std::move(s)), config(c), reconnect(clock, c.reconnect_first, c.reconnect_max) {}

  std::unique_ptr<IDbSession> session;
  Config config;
  core::SystemClock clock;
  dblog::ReconnectSchedule reconnect;  // clock 다음에 선언해야 한다 (선언 순서대로 생성되고, clock을 참조한다)

  mutable std::mutex mutex;
  std::condition_variable wake;      // 새 기록 또는 종료 요청 → DB 스레드
  std::condition_variable finished;  // DB 스레드 종료 → 소멸자
  std::deque<dblog::Row> queue;
  DbStats stats;
  std::vector<std::string> notices;
  bool connected = false;
  bool told_down = false;  // 끊김을 이미 알렸나: 재접속 실패마다 같은 문구를 되풀이하지 않는다
  bool stopping = false;
  bool done = false;

  void notice(std::string text) {
    if (notices.size() < kMaxNotices) notices.push_back(std::move(text));
  }

  void run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!(stopping && queue.empty())) {
      if (connected) write_batch(lock);
      else try_connect(lock);
    }
    lock.unlock();
    session->close();
    lock.lock();
    done = true;
    finished.notify_all();
  }

  void try_connect(std::unique_lock<std::mutex>& lock) {
    if (!reconnect.due()) {
      wake.wait_until(lock, reconnect.due_at(), [this] { return stopping; });
      return;
    }
    lock.unlock();
    const auto error = session->open();
    lock.lock();
    if (error) {
      const auto delay = reconnect.failed();
      if (!told_down) notice("db unavailable: " + *error + " (retry in " + std::to_string(delay.count()) + "ms)");
      told_down = true;
      return;
    }
    reconnect.succeeded();
    connected = true;
    told_down = false;
    notice("db connected");
  }

  void write_batch(std::unique_lock<std::mutex>& lock) {
    wake.wait(lock, [this] { return stopping || !queue.empty(); });
    if (queue.empty()) return;  // 종료 요청이고 남은 기록 없음
    const auto n = std::min(queue.size(), config.max_batch);
    std::vector<dblog::Row> batch(std::make_move_iterator(queue.begin()),
                                  std::make_move_iterator(queue.begin() + static_cast<std::ptrdiff_t>(n)));
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(n));
    lock.unlock();
    const auto error = session->write(batch);
    if (error) session->close();
    lock.lock();
    if (!error) {
      stats.written += batch.size();
      return;
    }
    // 연결을 버리고 다시 접속한다. 남은 대기열도 끊김으로 버려 "연결이 없으면 대기열은 비어 있다"를 지킨다
    stats.write_error += batch.size();
    stats.db_down += queue.size();
    queue.clear();
    connected = false;
    told_down = true;
    const auto delay = reconnect.failed();
    notice("db write failed, dropped " + std::to_string(batch.size()) + " records: " + *error + " (retry in " +
           std::to_string(delay.count()) + "ms)");
  }
};

DbSink::DbSink(std::unique_ptr<IDbSession> session, Config config)
    : state_(std::make_shared<State>(std::move(session), config)) {
  std::thread([s = state_] { s->run(); }).detach();
}

DbSink::~DbSink() {
  std::unique_lock<std::mutex> lock(state_->mutex);
  state_->stopping = true;
  state_->wake.notify_one();
  state_->finished.wait_for(lock, kFlushTimeout, [this] { return state_->done; });
}

void DbSink::publish(dblog::Row row) {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->connected) {
      ++state_->stats.db_down;
      return;
    }
    if (state_->queue.size() >= state_->config.max_queued) {
      ++state_->stats.queue_full;
      return;
    }
    state_->queue.push_back(std::move(row));
  }
  state_->wake.notify_one();
}

DbStats DbSink::stats() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->stats;
}

std::vector<std::string> DbSink::take_notices() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return std::exchange(state_->notices, {});
}

}  // namespace ugvc::app
