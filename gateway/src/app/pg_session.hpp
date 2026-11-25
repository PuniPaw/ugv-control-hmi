#pragma once

// PostgreSQL 연결 (libpq). DbSink의 DB 스레드에서만 쓴다 (SDD 5.1).
//
// libpq를 고른 이유는 SDD 5.1에 적었다: 쓰는 기능이 접속·준비된 문장·트랜잭션뿐이고,
// libpqxx는 패키지가 하나 더 늘고 오류를 예외로 던져 이 계층의 "실패하면 사유를 돌려준다"와 맞지 않는다.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "db_sink.hpp"

// libpq의 불완전 타입. 헤더에서 libpq-fe.h를 include하지 않아도 포인터는 선언할 수 있다.
struct pg_conn;

namespace ugvc::app {

class PgSession final : public IDbSession {
 public:
  // conninfo: libpq 접속 문자열 ("host=... dbname=..." 또는 "postgresql://...").
  // 비밀번호는 여기 넣지 말고 ~/.pgpass나 PGPASSWORD로 준다 (명령줄은 ps로 보인다).
  explicit PgSession(std::string conninfo);
  ~PgSession() override;

  std::optional<std::string> open() override;
  std::optional<std::string> write(const std::vector<dblog::Row>& rows) override;
  void close() override;

 private:
  // RAII: unique_ptr가 소멸할 때 PQfinish를 부른다. 어느 경로로 나가도 연결이 새지 않는다.
  struct ConnDeleter {
    void operator()(pg_conn* c) const;
  };

  std::optional<std::string> exec(const char* sql);
  std::string error() const;

  std::string conninfo_;
  std::unique_ptr<pg_conn, ConnDeleter> conn_;
};

}  // namespace ugvc::app
