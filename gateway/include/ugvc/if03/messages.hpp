#pragma once

// IF-03 GW → HMI JSON 메시지 (ICD 7.1). 정수 LSB → 공학 단위 변환은 여기서만 한다 (ADR-002).
// 공통 봉투: { "type": ..., "ts": <epoch ms>, "data": { ... } }

#include <cstdint>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/gateway/command_tracker.hpp"
#include "ugvc/gateway/gateway.hpp"
#include "ugvc/ingress/source.hpp"
#include "ugvc/link/link_monitor.hpp"

namespace ugvc::if03 {

nlohmann::json telemetry(const codec::Header& h, const codec::Telemetry& t, std::uint64_t rx_ts);
nlohmann::json detection(const codec::Detection& d, std::uint64_t rx_ts);
nlohmann::json stats(ingress::Source source, const gateway::SourceStats& s, std::uint64_t ts);
nlohmann::json link_status(ingress::Source source, link::LinkState state,
                           std::optional<std::uint64_t> last_rx_ts, std::uint64_t ts);
nlohmann::json link_status(const gateway::LinkTransition& t);  // 전이 시점의 link_status

// 명령 결과 확정 시 (ICD 7.1 cmd_result). cmd_id가 없으면 null
nlohmann::json cmd_result(const gateway::CommandResult& r, std::uint64_t ts);

// 1Hz 상태 보고: 송신원마다 stats, link_status 순서로 (ICD 7.1 "1Hz")
std::vector<nlohmann::json> snapshot(const gateway::Gateway& gw, std::uint64_t ts);

}  // namespace ugvc::if03
