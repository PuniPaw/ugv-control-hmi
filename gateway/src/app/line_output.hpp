#pragma once

// stdout JSON Lines 출력. 읽는 쪽이 멈춰도 io_context(판정 타이머, HEARTBEAT)를 막지 않는다.
//
// std::cout은 파이프가 가득 차면 쓰기에서 블록된다. GW는 단일 스레드 io_context라서 그동안
// 50ms 판정과 HEARTBEAT 1Hz가 멈추고, 3.0초가 지나면 SIM이 Failsafe에 들어간다 (REQ-SAFE-001).
// 그래서 파이프·터미널이면 비동기로 쓰고, 대기열이 상한(kMaxQueued)을 넘으면 새 줄은 버리고 센다.
// 일반 파일은 epoll에 등록할 수 없고 무한정 블록되지도 않으므로 동기로 쓴다.

#include <asio.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#if defined(ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ugvc::app {

class LineOutput {
 public:
  static constexpr std::size_t kMaxQueued = 1000;  // 약 1~2초 분량. 넘치면 표시용 줄만 잃는다

  explicit LineOutput(asio::io_context& io) {
#if defined(ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
    struct stat st {};
    if (::fstat(STDOUT_FILENO, &st) == 0 && !S_ISREG(st.st_mode)) {
      async_.emplace(io, ::dup(STDOUT_FILENO));  // dup: 소멸 시 원래 stdout을 닫지 않도록
    }
#else
    (void)io;
#endif
  }

  void publish(const nlohmann::json& msg) {
    std::string line = msg.dump();
    line.push_back('\n');
#if defined(ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
    if (async_) {
      if (queue_.size() >= kMaxQueued) {
        ++dropped_;
        return;
      }
      queue_.push_back(std::move(line));
      if (!writing_) write_next();
      return;
    }
#endif
    std::cout << line << std::flush;
  }

  std::uint64_t dropped() const { return dropped_; }

 private:
#if defined(ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
  void write_next() {
    writing_ = true;
    asio::async_write(*async_, asio::buffer(queue_.front()), [this](const asio::error_code& ec, std::size_t) {
      queue_.pop_front();
      if (ec) {
        queue_.clear();  // 읽는 쪽이 닫힘(EPIPE 등): 더 쓸 곳이 없다
        writing_ = false;
        return;
      }
      if (queue_.empty()) writing_ = false;
      else write_next();
    });
  }

  std::optional<asio::posix::stream_descriptor> async_;
  std::deque<std::string> queue_;
  bool writing_ = false;
#endif
  std::uint64_t dropped_ = 0;
};

}  // namespace ugvc::app
