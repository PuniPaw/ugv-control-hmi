#pragma once

// DB 로그 저장 (REQ-LOG-001·003, SDD 5.1). publish()는 대기열에 넣기만 하고 막히지 않는다.
//
// 구조는 LineSink와 같다: io 스레드는 상한 있는 대기열에 넣고, DbSink마다 하나인 전용 스레드가
// 접속·재접속과 실제 저장(블로킹 libpq 호출)을 한다. DB가 느리거나 멈추거나 없어도 io 스레드의
// 수신·검증·링크 판정·중계·재전송은 기다리지 않는다.
//
// 저장 실패는 종류를 가리지 않는다. 어떤 실패든 그 행 하나만 write_error로 세고, 대기열은 그대로 둔 채
// 연결을 닫고 다시 접속한다. 다시 접속하면 open()이 스키마와 준비된 문장을 다시 만들므로, 테이블 삭제 같은
// DB 상태 문제도 스스로 풀린다. 실패 원인을 연결·데이터로 가르는 판단은 두 번 틀렸다(PR #50 1·2차 리뷰).
//
// 연결 상태 (Link):
//   Down         — 접속하지 못했다. 들어온 기록은 db_down으로 버리고, 대기열은 비어 있다
//   Reconnecting — 저장이 실패해 다시 접속하는 중. 들어온 기록은 대기열에 쌓는다 (상한을 넘으면 queue_full)
//   Up           — 접속했다
// 접속한 뒤 한 번도 저장하지 못하고 다시 실패하면(접속 직후마다 끊는 서버, 계속 거절되는 행) 재접속 간격
// (ReconnectSchedule, 0.5s → 5s)을 적용한다. 한 번이라도 저장했으면 바로 다시 접속한다.
//
// 버리는 경우와 원인(DbStats):
//   queue_full  — 대기열(max_queued)이 찼다
//   db_down     — 접속하지 못한 동안 들어온 기록, 접속에 실패한 순간 대기열에 있던 기록, 종료 때 저장하지 못한 기록
//   write_error — 저장에 실패한 그 행
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

// DB 연결 하나. 실제는 PgSession(libpq), 단위 시험은 가짜. DB 스레드에서만 부른다.
class IDbSession {
 public:
  virtual ~IDbSession() = default;
  // 접속하고 스키마와 준비된 문장을 만든다. 실패하면 사유 (성공이면 nullopt)
  virtual std::optional<std::string> open() = 0;
  // 행 하나를 저장하고 바로 확정한다. 실패하면 사유. 실패 뒤 DbSink는 close()를 부르고 다시 open()한다
  virtual std::optional<std::string> write(const dblog::Row& row) = 0;
  virtual void close() = 0;
};

struct DbStats {
  std::uint64_t written = 0;
  std::uint64_t queue_full = 0;
  std::uint64_t db_down = 0;
  std::uint64_t write_error = 0;

  std::uint64_t dropped() const { return queue_full + db_down + write_error; }
};

// DB 스레드에서 일어난 일. 문구는 io 스레드(main의 report_db)가 만든다.
struct DbNotice {
  enum class Kind {
    Connected,      // 접속 성공
    ConnectFailed,  // 접속 실패 (사유가 직전 알림과 같으면 다시 내지 않는다)
    WriteFailed,    // 행 하나 저장 실패 → 다시 접속
    Stopped,        // DB 스레드가 예외로 멈춤. 이후 기록은 db_down
  };
  Kind kind;
  std::string reason;                     // Connected는 빈 문자열
  std::chrono::milliseconds retry_in{0};  // ConnectFailed·WriteFailed: 다음 접속 시도까지 (0 = 바로)
};

class DbSink {
 public:
  struct Config {
    std::size_t max_queued;
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
  // (DB가 멈춰도 GW 종료가 막히지 않게). 그때 대기열과 저장 중이던 행은 db_down으로 센다.
  // 두 번째 호출부터는 바로 돌아온다. 부른 뒤 publish()는 모두 db_down이다. stats()·take_notices()는 계속 쓸 수 있다.
  void close();

  DbStats stats() const;
  // 꺼내면 비워진다.
  std::vector<DbNotice> take_notices();

 private:
  struct State;
  std::shared_ptr<State> state_;  // DB 스레드와 공유: 떼어 낸 스레드가 끝날 때까지 살아 있어야 한다
};

}  // namespace ugvc::app
