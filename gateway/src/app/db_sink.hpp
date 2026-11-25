#pragma once

// DB 로그 저장 (REQ-LOG-001·003, SDD 5.1). publish()는 대기열에 넣기만 하고 막히지 않는다.
//
// 구조는 LineSink와 같다: io 스레드는 상한 있는 대기열에 넣고, DbSink마다 하나인 전용 스레드가
// 접속·재접속과 실제 저장(블로킹 libpq 호출)을 한다. DB가 느리거나 멈추거나 없어도 io 스레드의
// 수신·검증·링크 판정·중계·재전송은 기다리지 않는다.
//
// 저장 실패는 두 종류다 (WriteError::Kind). 둘을 섞으면 행 하나의 문제로 연결을 버리게 된다.
//   Data       — DB가 그 행만 거절했다. 연결은 그대로 쓰고 다음 행을 저장한다
//   Connection — 연결을 잃었다. 연결을 닫고 다시 접속한다
//
// 버리는 경우와 원인(DbStats):
//   queue_full  — 저장이 밀려 대기열(max_queued)이 찼다
//   db_down     — 연결이 없을 때 들어온 기록, 연결을 잃을 때 아직 저장하지 않은 기록
//   rejected    — DB가 그 행을 거절했다 (Data)
//   write_error — 저장하던 중 연결을 잃은 그 행 (Connection. 서버에 닿았는지 알 수 없다)
// 연결이 없는 동안 대기열은 비어 있다. 다시 접속하면 그 뒤에 들어온 기록부터 저장한다.
//
// 진단은 DB 스레드가 직접 쓰지 않는다. 일어난 일을 DbNotice(종류 + 사유)로 모으고, io 스레드가
// take_notices()로 꺼내 문구를 만든다. 종료 때 떼어 낸 DB 스레드가 이미 사라진 LineSink를 부르는
// 일이 없게 하기 위해서다.

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

struct WriteError {
  enum class Kind { Data, Connection };
  Kind kind;
  std::string reason;
};

// DB 연결 하나. 실제는 PgSession(libpq), 단위 시험은 가짜. DB 스레드에서만 부른다.
class IDbSession {
 public:
  virtual ~IDbSession() = default;
  // 접속하고 스키마를 만든다. 실패하면 사유 (성공이면 nullopt)
  virtual std::optional<std::string> open() = 0;
  // 행 하나를 저장하고 바로 확정한다(행마다 따로). 실패하면 종류와 사유.
  // Connection이면 DbSink가 close()를 부르고 다시 open()한다.
  virtual std::optional<WriteError> write(const dblog::Row& row) = 0;
  virtual void close() = 0;
};

struct DbStats {
  std::uint64_t written = 0;
  std::uint64_t queue_full = 0;
  std::uint64_t db_down = 0;
  std::uint64_t rejected = 0;
  std::uint64_t write_error = 0;

  std::uint64_t dropped() const { return queue_full + db_down + rejected + write_error; }
};

// DB 스레드에서 일어난 일. 문구는 io 스레드(main의 report_db)가 만든다.
struct DbNotice {
  enum class Kind {
    Connected,       // 접속 성공
    ConnectFailed,   // 접속 실패 (사유가 직전 알림과 같으면 다시 내지 않는다)
    ConnectionLost,  // 저장 중 연결을 잃음
    RowRejected,     // DB가 행 하나를 거절 (사유가 직전 알림과 같으면 다시 내지 않는다)
  };
  Kind kind;
  std::string reason;                       // Connected는 빈 문자열
  std::chrono::milliseconds retry_in{0};    // ConnectFailed만: 다음 시도까지
};

class DbSink {
 public:
  struct Config {
    std::size_t max_queued;
    std::size_t max_batch;  // DB 스레드가 대기열에서 한 번에 꺼내는 최대 행 수 (잠금 횟수를 줄인다)
    std::chrono::milliseconds reconnect_first = dblog::kReconnectFirst;
    std::chrono::milliseconds reconnect_max = dblog::kReconnectMax;
  };

  DbSink(std::unique_ptr<IDbSession> session, Config config);
  // close()와 같다.
  ~DbSink();
  DbSink(const DbSink&) = delete;
  DbSink& operator=(const DbSink&) = delete;

  // io 스레드에서 부른다. 호출 스레드를 막지 않는다 (잠금은 대기열 조작 동안만).
  void publish(dblog::Row row);

  // 남은 기록을 저장하도록 잠깐(kFlushTimeout) 기다리고, 끝나지 않으면 DB 스레드를 떼어 낸다
  // (DB가 멈춰도 GW 종료가 막히지 않게). 그때 대기열에 남은 기록은 db_down으로 센다.
  // 부른 뒤에는 publish()가 모두 db_down이 된다. stats()·take_notices()는 계속 쓸 수 있다.
  void close();

  DbStats stats() const;
  // 꺼내면 비워진다.
  std::vector<DbNotice> take_notices();

 private:
  struct State;
  std::shared_ptr<State> state_;  // DB 스레드와 공유: 떼어 낸 스레드가 끝날 때까지 살아 있어야 한다
};

}  // namespace ugvc::app
