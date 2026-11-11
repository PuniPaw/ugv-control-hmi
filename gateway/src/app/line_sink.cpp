#include "line_sink.hpp"

#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace ugvc::app {

namespace {

constexpr auto kFlushTimeout = std::chrono::milliseconds(500);

// 부분 쓰기와 EINTR을 처리해 전부 쓴다. EPIPE 등 다른 오류면 false.
bool write_all(int fd, const std::string& s) {
  std::size_t done = 0;
  while (done < s.size()) {
    const ssize_t n = ::write(fd, s.data() + done, s.size() - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    done += static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

// 잠금 규칙: 아래 필드는 모두 mutex로 보호한다. write(2)는 잠금을 놓고 한다.
struct LineSink::State {
  int fd;
  std::size_t max_queued;
  std::mutex mutex;
  std::condition_variable wake;      // 새 줄 또는 종료 요청 → 쓰기 스레드
  std::condition_variable finished;  // 쓰기 스레드 종료 → 소멸자
  std::deque<std::string> queue;
  std::uint64_t dropped = 0;
  bool broken = false;  // 쓰기 오류 뒤: 이후 줄은 모두 버린다
  bool stopping = false;
  bool done = false;

  void run() {
    std::unique_lock<std::mutex> lock(mutex);
    for (;;) {
      wake.wait(lock, [this] { return stopping || !queue.empty(); });
      if (queue.empty()) break;  // 종료 요청이고 남은 줄 없음
      const std::string line = std::move(queue.front());
      queue.pop_front();
      lock.unlock();
      const bool ok = write_all(fd, line);
      lock.lock();
      if (!ok) {
        broken = true;
        dropped += 1 + queue.size();
        queue.clear();
      }
    }
    done = true;
    finished.notify_all();
  }
};

LineSink::LineSink(int fd, std::size_t max_queued) : state_(std::make_shared<State>()) {
  state_->fd = fd;
  state_->max_queued = max_queued;
  std::thread([s = state_] { s->run(); }).detach();
}

LineSink::~LineSink() {
  std::unique_lock<std::mutex> lock(state_->mutex);
  state_->stopping = true;
  state_->wake.notify_one();
  state_->finished.wait_for(lock, kFlushTimeout, [this] { return state_->done; });
}

void LineSink::publish(std::string line) {
  line.push_back('\n');
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->broken || state_->queue.size() >= state_->max_queued) {
      ++state_->dropped;
      return;
    }
    state_->queue.push_back(std::move(line));
  }
  state_->wake.notify_one();
}

std::uint64_t LineSink::dropped() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->dropped;
}

}  // namespace ugvc::app
