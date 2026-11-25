#pragma once

// DB 로그 한 행 (REQ-LOG-001, SDD 5.1). 소켓·DB 없음, 순수 변환.
//
// io 스레드가 유효 패킷과 이벤트를 Row로 바꿔 DbSink 대기열에 넣고, DB 쓰기 스레드는 values를
// 그 테이블의 INSERT 문 매개변수($1, $2, ...)로 그대로 넘긴다. 값은 PostgreSQL 텍스트 형식이다.
// Row는 값만 담으므로 두 스레드가 같은 객체를 함께 만지지 않는다 (대기열로 넘기면 소유권이 옮겨 간다).
//
// 정수 필드는 ICD의 LSB 값 그대로 저장한다. 공학 단위 변환은 IF-03 경계에서만 한다 (ADR-002).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/gateway/command_tracker.hpp"
#include "ugvc/gateway/gateway.hpp"

namespace ugvc::dblog {

enum class Table : std::uint8_t { Telemetry, Detection, Event };

struct Row {
  Table table;
  std::vector<std::optional<std::string>> values;  // nullopt = SQL NULL. 순서는 insert_sql(table)의 열 순서
};

// 테이블·인덱스 생성문. 접속할 때마다 실행해도 같은 결과다 (IF NOT EXISTS).
const char* schema_sql();
// 테이블에 한 행을 넣는 문장. 매개변수 개수 = column_count(table)
const char* insert_sql(Table table);
int column_count(Table table);

Row telemetry_row(const codec::Header& header, const codec::Telemetry& t, std::uint64_t rx_epoch_ms);
Row detection_row(const codec::Header& header, const codec::Detection& d, std::uint64_t rx_epoch_ms);
// 이벤트: 링크 상태 전이 (kind "link", source VEHICLE·DETECTOR)
Row link_row(const gateway::LinkTransition& t);
// 이벤트: 명령 송신·재전송·결과·무시·모르는 ACK (kind "cmd_sent" 등, source NULL)
Row command_row(const gateway::CommandEvent& e, std::uint64_t at_epoch_ms);

}  // namespace ugvc::dblog
