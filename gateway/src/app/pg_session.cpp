#include "pg_session.hpp"

#include <libpq-fe.h>

#include <algorithm>
#include <array>

namespace ugvc::app {

namespace {

// 문장 이름은 테이블 순서와 같다 (dblog::Table 값 → 인덱스)
constexpr std::array<dblog::Table, 3> kTables{dblog::Table::Telemetry, dblog::Table::Detection, dblog::Table::Event};
constexpr std::array<const char*, 3> kStatementNames{"ugvc_insert_telemetry", "ugvc_insert_detection",
                                                     "ugvc_insert_event"};

const char* statement_name(dblog::Table t) { return kStatementNames[static_cast<std::size_t>(t)]; }

// PGresult도 RAII로: 결과마다 PQclear를 잊지 않게 한다.
struct ResultDeleter {
  void operator()(PGresult* r) const { PQclear(r); }
};
using Result = std::unique_ptr<PGresult, ResultDeleter>;

bool ok(const Result& r) {
  const auto status = PQresultStatus(r.get());
  return status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK;
}

}  // namespace

void PgSession::ConnDeleter::operator()(pg_conn* c) const { PQfinish(c); }

PgSession::PgSession(std::string conninfo) : conninfo_(std::move(conninfo)) {}

PgSession::~PgSession() = default;

// libpq 오류 문구는 여러 줄일 수 있다. 진단 한 줄에 담도록 개행을 공백으로 바꾼다 (진단 줄 위조 방지).
std::string PgSession::error() const {
  std::string m = conn_ ? PQerrorMessage(conn_.get()) : "not connected";
  std::replace(m.begin(), m.end(), '\n', ' ');
  std::replace(m.begin(), m.end(), '\r', ' ');
  while (!m.empty() && m.back() == ' ') m.pop_back();
  return m.empty() ? "unknown error" : m;
}

std::optional<std::string> PgSession::exec(const char* sql) {
  const Result r(PQexec(conn_.get(), sql));
  if (!ok(r)) return error();
  return std::nullopt;
}

std::optional<std::string> PgSession::open() {
  // 접속 문자열 앞에 기본값을 둔다. expand_dbname=1이면 dbname 자리의 접속 문자열이 펼쳐지고,
  // 뒤에 오는 값이 앞의 값을 덮으므로 사용자가 conninfo에 적은 값이 우선한다.
  // connect_timeout: 접속 시도 한 번의 상한(이름 해석은 포함하지 않는다). keepalives·tcp_user_timeout:
  // 네트워크가 소리 없이 끊긴 연결을 몇 초 안에 알아채 재접속으로 넘어가게 한다 (기본값이면 2시간 넘게 걸린다).
  // client_encoding=UTF8: 우리가 보내는 텍스트는 UTF-8이다. 정하지 않으면 서버 인코딩(예: EUC_KR)으로
  // 해석돼 한글 req_id 하나가 그 행을 깨뜨린다 (PR #50 리뷰 1).
  const char* const keys[] = {"connect_timeout", "application_name", "client_encoding", "keepalives_idle",
                              "keepalives_interval", "keepalives_count", "tcp_user_timeout", "dbname", nullptr};
  const char* const values[] = {"3", "ugvc_gateway", "UTF8", "5", "2", "3", "10000", conninfo_.c_str(), nullptr};
  conn_.reset(PQconnectdbParams(keys, values, /*expand_dbname=*/1));
  if (!conn_ || PQstatus(conn_.get()) != CONNECTION_OK) {
    auto e = error();
    conn_.reset();
    return e;
  }
  // 경고(NOTICE: 이미 있는 테이블 등)를 stderr로 직접 내보내지 않는다. 진단은 LineSink만 쓴다
  PQsetNoticeProcessor(conn_.get(), [](void*, const char*) {}, nullptr);
  // 행마다 확정하므로(write) 확정마다 디스크 동기화를 기다리지 않는다. 서버가 죽으면 마지막 1초 미만의
  // 기록을 잃을 수 있지만 DB가 깨지지는 않는다 (로그 용도에 맞는 선택, SDD 5.1).
  if (auto e = exec("SET synchronous_commit = off")) {
    conn_.reset();
    return e;
  }
  if (auto e = exec(dblog::schema_sql())) {
    conn_.reset();
    return e;
  }
  for (const auto t : kTables) {
    const Result r(PQprepare(conn_.get(), statement_name(t), dblog::insert_sql(t), dblog::column_count(t), nullptr));
    if (!ok(r)) {
      auto e = error();
      conn_.reset();
      return e;
    }
  }
  return std::nullopt;
}

// 행 하나를 자동 확정(autocommit)으로 저장한다. 행마다 따로 확정하므로 DB가 행 하나를 거절해도
// 다른 행은 그대로 저장된다. 실패하면 연결 상태로 종류를 가른다: 연결이 살아 있으면 그 행의 문제(Data),
// 아니면 연결을 잃은 것(Connection).
std::optional<WriteError> PgSession::write(const dblog::Row& row) {
  if (!conn_) return WriteError{WriteError::Kind::Connection, "not connected"};
  std::vector<const char*> params;
  params.reserve(row.values.size());
  for (const auto& v : row.values) params.push_back(v ? v->c_str() : nullptr);  // nullptr = SQL NULL
  const Result r(PQexecPrepared(conn_.get(), statement_name(row.table), static_cast<int>(params.size()),
                                params.data(), nullptr, nullptr, /*resultFormat=*/0));
  if (ok(r)) return std::nullopt;
  const auto kind = PQstatus(conn_.get()) == CONNECTION_OK ? WriteError::Kind::Data : WriteError::Kind::Connection;
  return WriteError{kind, error()};
}

void PgSession::close() { conn_.reset(); }

}  // namespace ugvc::app
