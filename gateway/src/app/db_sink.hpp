#pragma once

// DB 로그 저장 (REQ-LOG-001·003, SDD 5.1). publish()는 대기열에 넣기만 하고 막히지 않는다.
//
// 구조는 LineSink와 같다: io 스레드는 상한 있는 대기열에 넣고, DbSink마다 하나인 전용 스레드가
// 접속·재접속과 실제 저장(블로킹 libpq 호출)을 한다. DB가 느리거나 멈추거나 없어도 io 스레드의
// 수신·검증·링크 판정·중계·재전송은 기다리지 않는다.
//
// 버리는 경우와 원인(DbStats):
//   queue_full  — 저장이 밀려 대기열(max_queued)이 찼다
//   db_down     — 연결이 없을 때 들어온 기록, 연결이 끊길 때 대기열에 남아 있던 기록
//   write_error — 저장하던 배치가 실패했다 (그 배치 전체)
// 연결이 없는 동안 대기열은 비어 있다. 다시 접속하면 그 뒤에 들어온 기록부터 저장한다.
//
// 진단 문구(접속·끊김)는 DB 스레드가 직접 쓰지 않고 take_notices()로 io 스레드가 꺼내 쓴다.
// 종료 때 떼어 낸 DB 스레드가 이미 사라진 LineSink를 부르는 일이 없게 하기 위해서다.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ugvc/dblog/reconnect.hpp"
#include "ugvc/dblog/row.hpp"

namespace ugvc::app {

// DB 연결 하나. 실제는 PgSession(libpq), 단위 시험은 가짜. DB 스레드에서만 부른다.
class IDbSession {
 public:
  virtual ~IDbSession() = default;
  // 접속하고 스키마를 만든다. 실패하면 사유 (성공이면 nullopt)
  virtual std::optional<std::string> open() = 0;
  // 한 트랜잭션으로 저장한다. 실패하면 사유. 실패 뒤에는 close()를 부르고 다시 open()한다
  virtual std::optional<std::string> write(const std::vector<dblog::Row>& rows) = 0;
  virtual void close() = 0;
};

struct DbStats {
  std::uint64_t written = 0;
  std::uint64_t queue_full = 0;
  std::uint64_t db_down = 0;
  std::uint64_t write_error = 0;

  std::uint64_t dropped() const { return queue_full + db_down + write_error; }
};

class DbSink {
 public:
  struct Config {
    std::size_t max_queued;
    std::size_t max_batch;  // 한 트랜잭션에 넣는 최대 행 수
    std::chrono::milliseconds reconnect_first = dblog::kReconnectFirst;
    std::chrono::milliseconds reconnect_max = dblog::kReconnectMax;
  };

  DbSink(std::unique_ptr<IDbSession> session, Config config);
  // 남은 기록을 저장하도록 잠깐(kFlushTimeout) 기다리고, 끝나지 않으면 DB 스레드를 떼어 낸다
  // (DB가 멈춰도 GW 종료가 막히지 않게).
  ~DbSink();
  DbSink(const DbSink&) = delete;
  DbSink& operator=(const DbSink&) = delete;

  // io 스레드에서 부른다. 호출 스레드를 막지 않는다 (잠금은 대기열 조작 동안만).
  void publish(dblog::Row row);

  DbStats stats() const;
  // 접속·끊김 진단 문구. 꺼내면 비워진다.
  std::vector<std::string> take_notices();

 private:
  struct State;
  std::shared_ptr<State> state_;  // DB 스레드와 공유: 떼어 낸 스레드가 끝날 때까지 살아 있어야 한다
};

}  // namespace ugvc::app
