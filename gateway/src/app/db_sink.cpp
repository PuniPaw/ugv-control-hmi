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
constexpr std::size_t kMaxNotices = 16;  // io 스레드가 1Hz로 꺼낸다. 같은 사유는 되풀이하지 않으므로 드물다

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
  std::condition_variable finished;  // DB 스레드 종료 → close()
  std::deque<dblog::Row> queue;
  DbStats stats;
  std::vector<DbNotice> notices;
  std::optional<std::string> told_connect_failure;  // 알린 접속 실패 사유. 같은 사유는 되풀이하지 않는다
  std::optional<std::string> told_rejection;        // 알린 행 거절 사유
  bool connected = false;
  bool stopping = false;
  bool done = false;

  void notice(DbNotice n) {
    if (notices.size() < kMaxNotices) notices.push_back(std::move(n));
  }

  // 연결이 없어졌다: 대기열을 db_down으로 비워 "연결이 없으면 대기열은 비어 있다"를 지킨다.
  void drop_connection(std::uint64_t also_lost) {
    stats.db_down += also_lost + queue.size();
    queue.clear();
    connected = false;
  }

  void run() noexcept {
    try {
      loop();
    } catch (const std::exception& e) {
      // 메모리 부족 등. GW를 내리지 않고 DB 저장만 멈춘다. 이후 기록은 db_down으로 센다
      std::lock_guard<std::mutex> lock(mutex);
      drop_connection(0);
      stopping = true;
      notice({DbNotice::Kind::ConnectionLost, std::string("db thread stopped: ") + e.what(), {}});
    }
    session->close();
    std::lock_guard<std::mutex> lock(mutex);
    done = true;
    finished.notify_all();
  }

  void loop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!(stopping && queue.empty())) {
      if (connected) write_batch(lock);
      else try_connect(lock);
    }
  }

  void try_connect(std::unique_lock<std::mutex>& lock) {
    if (!reconnect.due()) {
      wake.wait_until(lock, reconnect.due_at(), [this] { return stopping; });
      return;
    }
    lock.unlock();
    auto error = session->open();
    lock.lock();
    if (error) {
      const auto delay = reconnect.failed();
      if (told_connect_failure != error) notice({DbNotice::Kind::ConnectFailed, *error, delay});
      told_connect_failure = std::move(error);
      return;
    }
    reconnect.succeeded();
    connected = true;
    told_connect_failure.reset();
    notice({DbNotice::Kind::Connected, {}, {}});
  }

  void write_batch(std::unique_lock<std::mutex>& lock) {
    wake.wait(lock, [this] { return stopping || !queue.empty(); });
    if (queue.empty()) return;  // 종료 요청이고 남은 기록 없음
    const auto n = std::min(queue.size(), config.max_batch);
    std::vector<dblog::Row> batch(std::make_move_iterator(queue.begin()),
                                  std::make_move_iterator(queue.begin() + static_cast<std::ptrdiff_t>(n)));
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(n));
    lock.unlock();

    std::size_t tried = 0;
    std::uint64_t written = 0;
    std::vector<std::string> rejections;
    std::optional<std::string> lost;
    for (const auto& row : batch) {
      auto error = session->write(row);
      ++tried;
      if (!error) {
        ++written;
      } else if (error->kind == WriteError::Kind::Data) {
        rejections.push_back(std::move(error->reason));  // 그 행만 잃고 같은 연결로 이어 간다
      } else {
        lost = std::move(error->reason);
        break;
      }
    }
    if (lost) session->close();

    lock.lock();
    stats.written += written;
    stats.rejected += rejections.size();
    for (auto& r : rejections) {
      if (told_rejection == r) continue;
      notice({DbNotice::Kind::RowRejected, r, {}});
      told_rejection = std::move(r);
    }
    if (lost) {
      ++stats.write_error;
      drop_connection(batch.size() - tried);
      // 재접속 일정은 마지막 성공 상태 그대로라 바로 다시 시도한다 (끊긴 뒤 DB가 이미 살아 있으면 곧 이어 저장)
      notice({DbNotice::Kind::ConnectionLost, std::move(*lost), {}});
    }
  }
};

DbSink::DbSink(std::unique_ptr<IDbSession> session, Config config)
    : state_(std::make_shared<State>(std::move(session), config)) {
  std::thread([s = state_] { s->run(); }).detach();
}

DbSink::~DbSink() { close(); }

void DbSink::close() {
  std::unique_lock<std::mutex> lock(state_->mutex);
  state_->stopping = true;
  state_->wake.notify_one();
  if (!state_->finished.wait_for(lock, kFlushTimeout, [this] { return state_->done; })) {
    // DB가 멈춰 다 저장하지 못했다. 떼어 낸 스레드에 남기는 기록은 여기서 버린 것으로 센다
    state_->drop_connection(0);
  }
}

void DbSink::publish(dblog::Row row) {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->connected || state_->stopping) {
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

std::vector<DbNotice> DbSink::take_notices() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return std::exchange(state_->notices, {});
}

}  // namespace ugvc::app
