#include "ugvc/dblog/row.hpp"

#include <algorithm>
#include <iterator>

#include <nlohmann/json.hpp>

#include "ugvc/core/overloaded.hpp"

namespace ugvc::dblog {
namespace {

using nlohmann::json;

// u64 시각(헤더 timestamp, capture_ts)은 bigint(i64)를 넘을 수 있어 numeric(20)에 넣는다.
// 받은 값이 검증 ⑧을 통과했다면 어떤 값이든 행 하나 때문에 배치 전체가 실패하지 않게 한다.
constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS telemetry (
  id           bigserial PRIMARY KEY,
  rx_ts_ms     bigint      NOT NULL,
  seq          bigint      NOT NULL,
  tx_ts_ms     numeric(20) NOT NULL,
  lat          integer     NOT NULL,
  lon          integer     NOT NULL,
  heading      integer     NOT NULL,
  speed        integer     NOT NULL,
  battery      smallint    NOT NULL,
  mode         smallint    NOT NULL,
  status_flags integer     NOT NULL
);
CREATE INDEX IF NOT EXISTS telemetry_rx_ts_ms ON telemetry (rx_ts_ms);
CREATE TABLE IF NOT EXISTS detection (
  id            bigserial PRIMARY KEY,
  rx_ts_ms      bigint      NOT NULL,
  seq           bigint      NOT NULL,
  tx_ts_ms      numeric(20) NOT NULL,
  frame_id      bigint      NOT NULL,
  capture_ts_ms numeric(20) NOT NULL,
  img_w         integer     NOT NULL,
  img_h         integer     NOT NULL,
  objects       jsonb       NOT NULL
);
CREATE INDEX IF NOT EXISTS detection_rx_ts_ms ON detection (rx_ts_ms);
CREATE TABLE IF NOT EXISTS event (
  id     bigserial PRIMARY KEY,
  ts_ms  bigint NOT NULL,
  kind   text   NOT NULL,
  source text,
  detail jsonb  NOT NULL
);
CREATE INDEX IF NOT EXISTS event_ts_ms ON event (ts_ms);
)sql";

std::string text(std::uint64_t v) { return std::to_string(v); }
std::string text(std::int64_t v) { return std::to_string(v); }

// PostgreSQL의 text·jsonb는 NUL 문자(JSON "\u0000")를 받지 않는다. HMI가 보낸 req_id에 NUL이 있으면
// 그 행 때문에 배치 전체가 실패하므로, 저장 전에 U+FFFD로 바꾼다. 다른 문자는 그대로 둔다.
std::string without_nul(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    if (c == '\0') out += "\xEF\xBF\xBD";
    else out += c;
  }
  return out;
}

// 잘못된 UTF-8은 예외 대신 U+FFFD로 바꿔 직렬화한다 (req_id는 HMI 입력).
std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

json cmd_id_json(const std::optional<std::uint32_t>& id) { return id ? json(*id) : json(nullptr); }

Row event(std::uint64_t ts, const char* kind, std::optional<std::string> source, const json& detail) {
  return {Table::Event, {text(ts), std::string(kind), std::move(source), dump(detail)}};
}

}  // namespace

const char* schema_sql() { return kSchema; }

const char* insert_sql(Table table) {
  switch (table) {
    case Table::Telemetry:
      return "INSERT INTO telemetry (rx_ts_ms, seq, tx_ts_ms, lat, lon, heading, speed, battery, mode, status_flags) "
             "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)";
    case Table::Detection:
      return "INSERT INTO detection (rx_ts_ms, seq, tx_ts_ms, frame_id, capture_ts_ms, img_w, img_h, objects) "
             "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)";
    case Table::Event:
      return "INSERT INTO event (ts_ms, kind, source, detail) VALUES ($1, $2, $3, $4)";
  }
  return nullptr;
}

int column_count(Table table) {
  switch (table) {
    case Table::Telemetry: return 10;
    case Table::Detection: return 8;
    case Table::Event: return 4;
  }
  return 0;
}

Row telemetry_row(const codec::Header& h, const codec::Telemetry& t, std::uint64_t rx_epoch_ms) {
  return {Table::Telemetry,
          {text(rx_epoch_ms), text(std::uint64_t{h.seq}), text(h.timestamp_ms), text(std::int64_t{t.lat}),
           text(std::int64_t{t.lon}), text(std::uint64_t{t.heading}), text(std::uint64_t{t.speed}),
           text(std::uint64_t{t.battery}), text(std::uint64_t{t.mode}), text(std::uint64_t{t.status_flags})}};
}

Row detection_row(const codec::Header& h, const codec::Detection& d, std::uint64_t rx_epoch_ms) {
  json objects = json::array();
  std::transform(d.objects.begin(), d.objects.end(), std::back_inserter(objects), [](const codec::DetectedObject& o) {
    return json{{"class_id", o.class_id}, {"confidence", o.confidence}, {"x", o.x}, {"y", o.y}, {"w", o.w}, {"h", o.h}};
  });
  return {Table::Detection,
          {text(rx_epoch_ms), text(std::uint64_t{h.seq}), text(h.timestamp_ms), text(std::uint64_t{d.frame_id}),
           text(d.capture_ts), text(std::uint64_t{d.img_w}), text(std::uint64_t{d.img_h}), dump(objects)}};
}

Row link_row(const gateway::LinkTransition& t) {
  const json detail{{"from", link::to_string(t.change.from)},
                    {"to", link::to_string(t.change.to)},
                    {"last_rx_ts", t.change.last_rx_epoch_ms ? json(*t.change.last_rx_epoch_ms) : json(nullptr)}};
  return event(t.change.at_epoch_ms, "link", std::string(ingress::to_string(t.source)), detail);
}

Row command_row(const gateway::CommandEvent& e, std::uint64_t at) {
  return std::visit(
      core::overloaded{
          [&](const gateway::CommandSent& s) {
            json detail{{"req_id", without_nul(s.req_id)}, {"cmd_id", s.cmd_id},
                        {"cmd_type", codec::name_of(s.cmd_type)}, {"attempt", s.attempt}};
            if (s.cmd_type == codec::CmdType::Waypoint) {
              detail["lat"] = s.lat;  // 1e-7 deg 정수 그대로
              detail["lon"] = s.lon;
            }
            return event(at, "cmd_sent", std::nullopt, detail);
          },
          [&](const gateway::CommandResult& r) {
            return event(at, "cmd_result", std::nullopt,
                         {{"req_id", without_nul(r.req_id)}, {"cmd_id", cmd_id_json(r.cmd_id)},
                          {"result", gateway::name_of(r.result)}, {"attempts", r.attempts},
                          {"repeated", r.repeated}});
          },
          [&](const gateway::CommandIgnored& i) {
            return event(at, "cmd_ignored", std::nullopt, {{"req_id", without_nul(i.req_id)}, {"reason", i.reason}});
          },
          [&](const gateway::UnknownAck& u) {
            return event(at, "cmd_unknown_ack", std::nullopt, {{"cmd_id", u.cmd_id}, {"result", u.result}});
          },
      },
      e);
}

}  // namespace ugvc::dblog
