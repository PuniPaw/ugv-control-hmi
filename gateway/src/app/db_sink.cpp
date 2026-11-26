#include "db_sink.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "ugvc/core/clock.hpp"

namespace ugvc::app {

namespace {

constexpr auto kFlushTimeout = std::chrono::milliseconds(500);
constexpr std::size_t kMaxNotices = 16;  // io 스레드가 1Hz로 꺼낸다. 같은 사유는 잇달아 되풀이하지 않는다

}  // namespace

// 잠금 규칙: session을 뺀 필드는 모두 mutex로 보호한다. session은 DB 스레드만 쓰고, 그 호출(블로킹)은
// 잠금을 놓고 한다. 그래서 DB 호출이 오래 걸려도 publish()는 기다리지 않는다.
struct DbSink::State {
  enum class Link { Down, Reconnecting, Up };

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
  std::size_t in_flight = 0;  // 대기열에서 꺼내 저장 중인 행 (0 또는 1)
  DbStats stats;
  std::vector<DbNotice> notices;
  std::optional<std::string> told_connect_failure;  // 직전에 알린 사유. 같은 사유는 잇달아 알리지 않는다
  std::optional<std::string> told_write_failure;
  Link link = Link::Down;
  bool proven = false;     // 이번 연결로 한 번이라도 저장했나: 아니면 저장 실패 뒤 재접속에 간격을 둔다
  bool stopping = false;   // close() 또는 DB 스레드 예외
  bool closed = false;     // close()를 불렀다
  bool abandoned = false;  // close()가 기다리다 떼어 냈다: 저장 중이던 행은 close()가 이미 셌다
  bool done = false;

  void notice(DbNotice n) {
    if (notices.size() < kMaxNotices) notices.push_back(std::move(n));
  }

  // 저장하지 못하고 남은 기록을 db_down으로 센다
  void drop_pending() {
    stats.db_down += queue.size() + in_flight;
    queue.clear();
    in_flight = 0;
  }

  void run() noexcept {
    try {
      loop();
    } catch (const std::exception& e) {
      stop_on_error(e.what());
    } catch (...) {
      stop_on_error("unknown exception");
    }
    session->close();
    std::lock_guard<std::mutex> lock(mutex);
    done = true;
    finished.notify_all();
  }

  // 메모리 부족 등. GW를 내리지 않고 DB 저장만 멈춘다. 이후 기록은 db_down
  void stop_on_error(const char* what) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!abandoned) drop_pending();
    link = Link::Down;
    stopping = true;
    notice({DbNotice::Kind::Stopped, what, {}});
  }

  void loop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!(stopping && queue.empty())) {
      if (link == Link::Up) {
        write_one(lock);
      } else if (stopping) {
        drop_pending();  // 종료 중에는 다시 접속하지 않는다
      } else {
        try_connect(lock);
      }
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
      link = Link::Down;
      drop_pending();
      if (told_connect_failure != error) notice({DbNotice::Kind::ConnectFailed, *error, delay});
      told_connect_failure = std::move(error);
      return;
    }
    link = Link::Up;
    proven = false;
    told_connect_failure.reset();
    notice({DbNotice::Kind::Connected, {}, {}});
  }

  // 대기열에서 한 행을 꺼내 저장하고 바로 센다
  void write_one(std::unique_lock<std::mutex>& lock) {
    wake.wait(lock, [this] { return stopping || !queue.empty(); });
    if (queue.empty()) return;  // 종료 요청이고 남은 기록 없음
    const dblog::Row row = std::move(queue.front());
    queue.pop_front();
    in_flight = 1;
    lock.unlock();
    auto error = session->write(row);
    if (error) session->close();
    lock.lock();
    if (abandoned) return;
    in_flight = 0;
    if (!error) {
      ++stats.written;
      if (!proven) reconnect.succeeded();
      proven = true;
      told_write_failure.reset();
      return;
    }
    ++stats.write_error;
    // 한 번이라도 저장한 연결이면 바로, 아니면 간격을 두고 다시 접속한다. 대기열은 그대로 둔다
    const auto delay = proven ? std::chrono::milliseconds{0} : reconnect.failed();
    link = Link::Reconnecting;
    if (told_write_failure != error) notice({DbNotice::Kind::WriteFailed, *error, delay});
    told_write_failure = std::move(error);
  }
};

DbSink::DbSink(std::unique_ptr<IDbSession> session, Config config)
    : state_(std::make_shared<State>(std::move(session), config)) {
  std::thread([s = state_] { s->run(); }).detach();
}

DbSink::~DbSink() { close(); }

void DbSink::close() {
  std::unique_lock<std::mutex> lock(state_->mutex);
  if (state_->closed) return;
  state_->closed = true;
  state_->stopping = true;
  state_->wake.notify_one();
  if (!state_->finished.wait_for(lock, kFlushTimeout, [this] { return state_->done; })) {
    // DB가 멈춰 다 저장하지 못했다. 떼어 낸 스레드에 남기는 기록(대기열, 저장 중인 행)은 여기서 센다
    state_->drop_pending();
    state_->abandoned = true;
  }
}

void DbSink::publish(dblog::Row row) {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping || state_->link == State::Link::Down) {
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
