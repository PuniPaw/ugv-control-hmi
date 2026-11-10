#pragma once

// IF-03 GW → HMI JSON 메시지 (ICD 7.1). 정수 LSB → 공학 단위 변환은 여기서만 한다 (ADR-002).
// 공통 봉투: { "type": ..., "ts": <epoch ms>, "data": { ... } }

#include <cstdint>
#include <optional>

#include <nlohmann/json.hpp>

#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/gateway/gateway.hpp"
#include "ugvc/ingress/source.hpp"
#include "ugvc/link/link_monitor.hpp"

namespace ugvc::if03 {

nlohmann::json telemetry(const codec::Header& h, const codec::Telemetry& t, std::uint64_t rx_ts);
nlohmann::json detection(const codec::Detection& d, std::uint64_t rx_ts);
nlohmann::json stats(ingress::Source source, const gateway::SourceStats& s, std::uint64_t ts);
nlohmann::json link_status(ingress::Source source, link::LinkState state,
                           std::optional<std::uint64_t> last_rx_ts, std::uint64_t ts);

}  // namespace ugvc::if03
