"""COMMAND (0x10, 13B, GW → SIM) — ICD 6.3. ACK (0x11, 5B, SIM → GW) — ICD 6.4."""
from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum

MSG_COMMAND = 0x10
MSG_ACK = 0x11
MSG_HEARTBEAT = 0x20
COMMAND_SIZE = 13
ACK_SIZE = 5

_COMMAND = struct.Struct(">IBii")  # cmd_id u32 | cmd_type u8 | lat i32 | lon i32
_ACK = struct.Struct(">IB")        # cmd_id u32 | result u8
assert _COMMAND.size == COMMAND_SIZE and _ACK.size == ACK_SIZE


class CmdType(IntEnum):
    """표 6-4."""
    STOP = 0x01
    RTB = 0x02
    WAYPOINT = 0x03


class AckResult(IntEnum):
    """표 6-5."""
    ACCEPTED = 0
    REJECTED_INVALID = 1
    REJECTED_STATE = 2


@dataclass(frozen=True)
class Command:
    cmd_id: int
    cmd_type: int
    lat: int = 0  # 1e-7 deg, WAYPOINT만 유효
    lon: int = 0

    def to_payload(self) -> bytes:
        return _COMMAND.pack(self.cmd_id, self.cmd_type, self.lat, self.lon)

    @classmethod
    def from_payload(cls, payload: bytes) -> "Command | None":
        if len(payload) != COMMAND_SIZE:
            return None
        return cls(*_COMMAND.unpack(payload))


@dataclass(frozen=True)
class Ack:
    cmd_id: int
    result: int

    def to_payload(self) -> bytes:
        return _ACK.pack(self.cmd_id, self.result)

    @classmethod
    def from_payload(cls, payload: bytes) -> "Ack | None":
        if len(payload) != ACK_SIZE:
            return None
        return cls(*_ACK.unpack(payload))
