#include "ugvc/codec/route.hpp"

namespace ugvc::codec {

Bytes encode_route(const Route& r) {
  Bytes out;
  out.reserve(route_payload_size(r.waypoints.size()));
  write_u8(out, static_cast<std::uint8_t>(r.waypoints.size()));
  for (const auto& w : r.waypoints) {
    write_i32_be(out, w.lat);
    write_i32_be(out, w.lon);
  }
  return out;
}

std::optional<Route> decode_route(const std::uint8_t* p, std::size_t len) {
  if (len < kRouteHeadSize) {
    return std::nullopt;
  }
  const std::uint8_t count = read_u8(p);
  if (len != route_payload_size(count)) {
    return std::nullopt;
  }
  Route r;
  r.waypoints.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t* w = p + kRouteHeadSize + i * kWaypointSize;
    r.waypoints.push_back({read_i32_be(w), read_i32_be(w + 4)});
  }
  return r;
}

}  // namespace ugvc::codec
