#include "ugvc/if03/messages.hpp"

namespace ugvc::if03 {
namespace {

using namespace ugvc::codec;
using nlohmann::json;

json envelope(const char* type, std::uint64_t ts, json data) {
  return {{"type", type}, {"ts", ts}, {"data", std::move(data)}};
}

const char* mode_name(std::uint8_t mode) {
  switch (static_cast<Mode>(mode)) {
    case Mode::Standby: return "STANDBY";
    case Mode::Mission: return "MISSION";
    case Mode::Stopped: return "STOPPED";
    case Mode::Rtb: return "RTB";
    case Mode::Fault: return "FAULT";
  }
  return "UNKNOWN";  // 검증 ⑧을 통과한 값에는 오지 않는다
}

const char* class_name(std::uint8_t c) {
  switch (static_cast<ClassId>(c)) {
    case ClassId::Person: return "PERSON";
    case ClassId::Vehicle: return "VEHICLE";
    case ClassId::ArmoredVehicle: return "ARMORED_VEHICLE";
    case ClassId::Unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
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
                  {{"lat", t.lat * 1e-7},
                   {"lon", t.lon * 1e-7},
                   {"heading_deg", t.heading * 0.01},
                   {"speed_mps", t.speed * 0.01},
                   {"battery_pct", t.battery},
                   {"mode", mode_name(t.mode)},
                   {"flags", flag_names(t.status_flags)},
                   {"seq", h.seq},
                   {"rx_ts", rx_ts}});
}

json detection(const Detection& d, std::uint64_t rx_ts) {
  json objects = json::array();
  for (const auto& o : d.objects) {
    objects.push_back({{"class", class_name(o.class_id)},
                       {"conf", o.confidence * 1e-4},
                       {"x", o.x},
                       {"y", o.y},
                       {"w", o.w},
                       {"h", o.h}});
  }
  return envelope("detection", rx_ts,
                  {{"frame_id", d.frame_id},
                   {"capture_ts", d.capture_ts},
                   {"img_w", d.img_w},
                   {"img_h", d.img_h},
                   {"objects", std::move(objects)}});
}

json stats(ingress::Source source, const gateway::SourceStats& s, std::uint64_t ts) {
  json errors = json::object();
  for (std::size_t i = 0; i < ingress::kRejectReasonCount; ++i) {
    const auto r = static_cast<ingress::RejectReason>(i);
    errors[ingress::to_string(r)] = s.errors[r];
  }
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

}  // namespace ugvc::if03
