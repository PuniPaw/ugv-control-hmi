// LineSink: 읽는 쪽이 멈추거나 사라져도 publish()가 막히지 않고, 잃은 줄을 세며, fd 플래그를 바꾸지 않는다.
#include "line_sink.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

using ugvc::app::LineSink;
using Clock = std::chrono::steady_clock;

struct Pipe {
  int r = -1;
  int w = -1;
  Pipe() {
    int fds[2];
    if (::pipe(fds) != 0) throw std::runtime_error("pipe");
    r = fds[0];
    w = fds[1];
  }
  ~Pipe() {
    if (r >= 0) ::close(r);
    if (w >= 0) ::close(w);
  }
  void close_read() {
    ::close(r);
    r = -1;
  }
};

// 쓰기 끝이 닫힐 때까지 읽어 줄 수를 센다
std::size_t count_lines(int fd) {
  std::size_t lines = 0;
  char buf[4096];
  for (;;) {
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n <= 0) return lines;
    for (ssize_t i = 0; i < n; ++i) lines += buf[i] == '\n';
  }
}

const std::string kLine(200, 'x');

}  // namespace

TEST(LineSink, REQ_COM_007_publish_does_not_block_when_reader_stalls) {
  Pipe p;
  constexpr std::size_t kPublished = 5000;  // 약 1MB: 파이프(64KB)와 대기열(100줄)을 모두 넘는다
  std::thread reader;
  {
    LineSink sink(p.w, 100);
    const auto t0 = Clock::now();
    for (std::size_t i = 0; i < kPublished; ++i) sink.publish(kLine);
    EXPECT_LT(Clock::now() - t0, std::chrono::milliseconds(500));  // 읽는 쪽이 없어도 바로 돌아온다
    EXPECT_GT(sink.dropped(), 0u);
    reader = std::thread([&] { (void)count_lines(p.r); });  // 읽는 쪽이 돌아온다
  }  // 소멸자: 남은 줄을 쓰고 끝낸다
  ::close(p.w);
  p.w = -1;
  reader.join();
}

TEST(LineSink, every_published_line_is_written_or_counted) {
  Pipe p;
  constexpr std::size_t kPublished = 3000;
  std::size_t written = 0;
  std::thread reader;
  std::uint64_t dropped = 0;
  {
    LineSink sink(p.w, 50);
    for (std::size_t i = 0; i < kPublished; ++i) {
      sink.publish(kLine);
      if (i == kPublished / 2) reader = std::thread([&] { written = count_lines(p.r); });  // 중간부터 읽기 시작
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    dropped = sink.dropped();
  }
  ::close(p.w);
  p.w = -1;
  reader.join();
  EXPECT_EQ(written + dropped, kPublished);
}

TEST(LineSink, counts_lines_lost_after_reader_closes) {
  std::signal(SIGPIPE, SIG_IGN);  // GW main과 같은 설정: EPIPE를 쓰기 오류로 받는다
  Pipe p;
  p.close_read();
  LineSink sink(p.w, 1000);
  constexpr std::size_t kPublished = 20;
  for (std::size_t i = 0; i < kPublished; ++i) sink.publish(kLine);
  const auto deadline = Clock::now() + std::chrono::seconds(2);
  while (sink.dropped() < kPublished && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  EXPECT_EQ(sink.dropped(), kPublished);
}

TEST(LineSink, leaves_fd_flags_unchanged) {
  // stdout·stderr가 열린 파일 설명을 공유할 때(2>&1, 터미널) O_NONBLOCK을 걸면 stderr와 터미널이 함께 바뀐다
  Pipe p;
  const int before = ::fcntl(p.w, F_GETFL);
  std::thread reader([&] { (void)count_lines(p.r); });
  {
    LineSink sink(p.w, 1000);
    for (int i = 0; i < 100; ++i) sink.publish(kLine);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(::fcntl(p.w, F_GETFL), before);
  }
  EXPECT_EQ(::fcntl(p.w, F_GETFL), before);
  ::close(p.w);
  p.w = -1;
  reader.join();
}
