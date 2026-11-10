"""TELEMETRY (msg_type 0x01, payload 16B) — ICD 6.1.

필드는 ICD의 정수 값(LSB 단위)을 그대로 담는다 (ADR-002).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum, IntFlag

MSG_TELEMETRY = 0x01
PAYLOAD_SIZE = 16

# lat i32 | lon i32 | heading u16 | speed u16 | battery u8 | mode u8 | status_flags u16
_PAYLOAD = struct.Struct(">iiHHBBH")
assert _PAYLOAD.size == PAYLOAD_SIZE


class Mode(IntEnum):
    """표 6-1."""
    STANDBY = 0
    MISSION = 1
    STOPPED = 2
    RTB = 3
    FAULT = 4


class Flags(IntFlag):
    """표 6-2. 비트 4–15는 예약."""
    GPS_VALID = 1 << 0
    CAMERA_OK = 1 << 1
    BATTERY_LOW = 1 << 2
    FAILSAFE_ACTIVE = 1 << 3


@dataclass(frozen=True)
class Telemetry:
    lat: int           # 1e-7 deg
    lon: int           # 1e-7 deg
    heading: int       # 0.01 deg
    speed: int         # 0.01 m/s
    battery: int       # 1 %
    mode: int          # Mode
    status_flags: int  # Flags

    def to_payload(self) -> bytes:
        return _PAYLOAD.pack(self.lat, self.lon, self.heading, self.speed,
                             self.battery, self.mode, self.status_flags)

    @classmethod
    def from_payload(cls, payload: bytes) -> "Telemetry | None":
        if len(payload) != PAYLOAD_SIZE:
            return None
        return cls(*_PAYLOAD.unpack(payload))
