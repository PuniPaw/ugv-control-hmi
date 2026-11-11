#include "ugvc/if03/messages.hpp"

#include <algorithm>
#include <iterator>

namespace ugvc::if03 {
namespace {

using namespace ugvc::codec;
using nlohmann::json;

json envelope(const char* type, std::uint64_t ts, json data) {
  return {{"type", type}, {"ts", ts}, {"data", std::move(data)}};
}

json flag_names(std::uint16_t f) {
  json out = json::array();
  if (f & flags::kGpsValid) out.push_back("GPS_VALID");
  if (f & flags::kCameraOk) out.push_back("CAMERA_OK");
  if (f & flags::kBatteryLow) out.push_back("BATTERY_LOW");
  if (f & flags::kFailsafeActive) out.push_back("FAILSAFE_ACTIVE");
  return out;  // 예약 비트(4–15)는 무시 (ICD 3장)
}

}  // namespace

json telemetry(const Header& h, const Telemetry& t, std::uint64_t rx_ts) {
  return envelope("telemetry", rx_ts,
                  {{"lat", t.lat / 1e7},  // 곱하기(× 1e-7)는 1e-7이 이진수로 정확하지 않아 36.123456399999995 같은 값이 나온다
                   {"lon", t.lon / 1e7},
                   {"heading_deg", t.heading / 100.0},
                   {"speed_mps", t.speed / 100.0},
                   {"battery_pct", t.battery},
                   {"mode", name_of(static_cast<Mode>(t.mode))},  // 검증 ⑧을 통과한 값이라 nullptr가 아니다
                   {"flags", flag_names(t.status_flags)},
                   {"seq", h.seq},
                   {"rx_ts", rx_ts}});
}

json detection(const Detection& d, std::uint64_t rx_ts) {
  json objects = json::array();
  std::transform(d.objects.begin(), d.objects.end(), std::back_inserter(objects), [](const DetectedObject& o) {
    return json{{"class", name_of(static_cast<ClassId>(o.class_id))},
                {"conf", o.confidence / 1e4},
                {"x", o.x},
                {"y", o.y},
                {"w", o.w},
                {"h", o.h}};
  });
  return envelope("detection", rx_ts,
                  {{"frame_id", d.frame_id},
                   {"capture_ts", d.capture_ts},
                   {"img_w", d.img_w},
                   {"img_h", d.img_h},
                   {"objects", std::move(objects)}});
}

json stats(ingress::Source source, const gateway::SourceStats& s, std::uint64_t ts) {
  json errors = json::object();
  for (const auto r : ingress::kAllRejectReasons) errors[ingress::to_string(r)] = s.errors[r];
  return envelope("stats", ts,
                  {{"source", ingress::to_string(source)},
                   {"loss_rate_10s", s.loss_rate_10s ? json(*s.loss_rate_10s) : json(nullptr)},
                   {"rx_count", s.rx_count},
                   {"lost_count", s.lost_count},
                   {"errors", std::move(errors)}});
}

json link_status(ingress::Source source, link::LinkState state,
                 std::optional<std::uint64_t> last_rx_ts, std::uint64_t ts) {
  return envelope("link_status", ts,
                  {{"source", ingress::to_string(source)},
                   {"state", link::to_string(state)},
                   {"last_rx_ts", last_rx_ts ? json(*last_rx_ts) : json(nullptr)}});
}

json link_status(const gateway::LinkTransition& t) {
  return link_status(t.source, t.change.to, t.change.last_rx_epoch_ms, t.change.at_epoch_ms);
}

std::vector<json> snapshot(const gateway::Gateway& gw, std::uint64_t ts) {
  std::vector<json> out;
  for (const ingress::Source s : ingress::kAllSources) {
    out.push_back(stats(s, gw.stats(s), ts));
    const auto& link = gw.link(s);
    out.push_back(link_status(s, link.state(), link.last_rx_epoch_ms(), ts));
  }
  return out;
}

}  // namespace ugvc::if03
