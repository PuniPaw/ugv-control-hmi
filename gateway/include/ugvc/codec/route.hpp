#pragma once

// ROUTE (msg_type 0x03, payload 1 + 8 × count) — ICD 6.6. SIM이 보고하는 남은 경로점 목록.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "ugvc/codec/bytes.hpp"
#include "ugvc/codec/msg_type.hpp"

namespace ugvc::codec {

inline constexpr std::size_t kRouteHeadSize = 1;  // count u8
inline constexpr std::size_t kWaypointSize = 8;   // lat i32, lon i32
inline constexpr std::size_t kMaxWaypoints = 32;  // count: 0 ~ 32 (REQ-CMD-007)

struct Waypoint {
  std::int32_t lat = 0;  // 1e-7 deg, 범위는 TELEMETRY와 같다
  std::int32_t lon = 0;

  bool operator==(const Waypoint& o) const { return lat == o.lat && lon == o.lon; }
};

struct Route {
  std::vector<Waypoint> waypoints;  // 방문 순서. 첫 원소가 지금 향하는 경로점, count = size()

  bool operator==(const Route& o) const { return waypoints == o.waypoints; }
};

constexpr std::size_t route_payload_size(std::size_t count) { return kRouteHeadSize + kWaypointSize * count; }

Bytes encode_route(const Route& r);

// payload 길이가 1 + 8 × count와 맞지 않으면 nullopt. 값 범위(count ≤ 32, 좌표)는 검사하지 않는다.
std::optional<Route> decode_route(const std::uint8_t* payload, std::size_t len);

}  // namespace ugvc::codec
