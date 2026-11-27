"""ROUTE (msg_type 0x03, payload 1 + 8 × count, SIM → GW) — ICD 6.6.

SIM이 가진 남은 경로점 목록을 방문 순서대로 담는다. 좌표는 ICD 정수 LSB(1e-7 deg) 그대로다 (ADR-002).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

MAX_WAYPOINTS = 32  # count 0 ~ 32 (REQ-CMD-007)
WAYPOINT_SIZE = 8

_COUNT = struct.Struct(">B")     # count u8
_WAYPOINT = struct.Struct(">ii")  # lat i32 | lon i32 (1e-7 deg)
assert _WAYPOINT.size == WAYPOINT_SIZE


def payload_size(count: int) -> int:
    return _COUNT.size + WAYPOINT_SIZE * count


@dataclass(frozen=True)
class Route:
    waypoints: tuple[tuple[int, int], ...] = ()  # (lat, lon) 1e-7 deg. 첫 원소가 지금 향하는 경로점

    def to_payload(self) -> bytes:
        return _COUNT.pack(len(self.waypoints)) + b"".join(_WAYPOINT.pack(lat, lon) for lat, lon in self.waypoints)

    @classmethod
    def from_payload(cls, payload: bytes) -> "Route | None":
        """payload 길이가 1 + 8 × count와 맞지 않으면 None. 값 범위는 검사하지 않는다."""
        if len(payload) < _COUNT.size or len(payload) != payload_size(payload[0]):
            return None
        return cls(tuple(_WAYPOINT.iter_unpack(payload[_COUNT.size:])))
