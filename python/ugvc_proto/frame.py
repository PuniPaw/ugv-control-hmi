"""공통 헤더(18B) + payload(N) + CRC16(2B) 프레임 (ICD 3~4장, ADR-002).

모든 다바이트 필드는 big-endian(struct 형식 문자 ">")으로 필드별로 직렬화한다.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

SYNC = 0xAA55
VERSION = 0x01
HEADER_SIZE = 18
CRC_SIZE = 2
FRAME_OVERHEAD = HEADER_SIZE + CRC_SIZE  # 20
MAX_FRAME_SIZE = 1200

# sync u16 | version u8 | msg_type u8 | seq u32 | timestamp u64 | payload_len u16
_HEADER = struct.Struct(">HBBIQH")
assert _HEADER.size == HEADER_SIZE


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 반사 없음, 최종 XOR 0."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


@dataclass(frozen=True)
class Header:
    sync: int
    version: int
    msg_type: int
    seq: int
    timestamp_ms: int
    payload_len: int


def encode_frame(msg_type: int, seq: int, timestamp_ms: int, payload: bytes = b"") -> bytes:
    """헤더 + payload + CRC 완성 프레임. sync·version은 ICD 고정값."""
    body = _HEADER.pack(SYNC, VERSION, msg_type, seq, timestamp_ms, len(payload)) + payload
    return body + struct.pack(">H", crc16_ccitt_false(body))


def decode_header(data: bytes) -> Header | None:
    """앞 18바이트를 헤더로 읽는다. 값 검증은 하지 않는다. 길이가 모자라면 None."""
    if len(data) < HEADER_SIZE:
        return None
    return Header(*_HEADER.unpack_from(data, 0))


def read_frame_crc(data: bytes) -> int | None:
    """프레임 끝 2바이트의 CRC 필드. 20바이트 미만이면 None."""
    if len(data) < FRAME_OVERHEAD:
        return None
    return struct.unpack_from(">H", data, len(data) - CRC_SIZE)[0]
