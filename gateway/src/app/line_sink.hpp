#pragma once

// fd 하나에 줄 단위로 쓰는 출력. publish()는 대기열에 넣기만 하고 막히지 않는다.
//
// GW는 단일 스레드 io_context라서, 그 스레드에서 블로킹 쓰기를 하면 읽는 쪽이 멈춘 동안
// 50ms 판정과 HEARTBEAT 1Hz가 함께 멈춘다 (3.0초면 SIM Failsafe, REQ-SAFE-001).
// 그래서 실제 write(2)는 LineSink마다 하나인 전용 스레드가 한다.
//
// O_NONBLOCK 비동기 쓰기를 쓰지 않는 이유: dup한 fd도 원래 fd와 열린 파일 설명(open file
// description)을 공유한다. 거기에 O_NONBLOCK을 걸면 `2>&1`로 같은 설명을 쓰는 stderr까지
// non-blocking이 되어 std::cerr가 EAGAIN으로 영구히 고장 나고, 종료 후에도 터미널에 남는다.
// 전용 스레드의 블로킹 쓰기는 fd 플래그를 바꾸지 않는다.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ugvc::app {

class LineSink {
 public:
  // fd는 빌려 쓴다 (닫지 않는다). max_queued를 넘는 줄은 버리고 센다.
  LineSink(int fd, std::size_t max_queued);
  // 남은 줄을 쓰도록 잠깐(kFlushTimeout) 기다린다. 읽는 쪽이 멈춰 끝나지 않으면 기다리지 않고
  // 쓰기 스레드를 떼어 낸다 (프로세스 종료가 출력 때문에 막히지 않게).
  ~LineSink();
  LineSink(const LineSink&) = delete;
  LineSink& operator=(const LineSink&) = delete;

  // 끝에 '\n'을 붙여 대기열에 넣는다. 대기열이 가득 찼거나, 읽는 쪽이 닫혀(EPIPE 등) 더 쓸 수
  // 없으면 버리고 dropped()를 늘린다. 호출 스레드를 막지 않는다.
  void publish(std::string line);

  // 지금까지 버린 줄 수: 대기열 초과 + 쓰기 오류로 잃은 줄.
  std::uint64_t dropped() const;

 private:
  struct State;
  std::shared_ptr<State> state_;  // 쓰기 스레드와 공유: 떼어 낸 스레드가 끝날 때까지 살아 있어야 한다
};

}  // namespace ugvc::app
