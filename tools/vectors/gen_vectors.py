#!/usr/bin/env python3
"""testdata/vectors/*.json 생성기.

C++(gateway)·Python(ugvc_proto) 구현과 독립된 기준을 만들기 위해
표준 라이브러리(struct, binascii.crc_hqx)만 사용한다. 우리 codec을 import하지 않는다.

binascii.crc_hqx(data, 0xFFFF)는 CRC-16/CCITT-FALSE와 같다 (poly 0x1021, init 0xFFFF, 반사 없음).

사용: python3 tools/vectors/gen_vectors.py  (저장소 루트에서)
"""
import binascii
import json
import struct
from pathlib import Path

OUT_DIR = Path(__file__).resolve().parents[2] / "testdata" / "vectors"

SYNC, VERSION = 0xAA55, 0x01
TS = 1794729600000  # ICD 7.1 예시 기준 시각

# ---------------------------------------------------------------- frame.json (ICD 4장)


def frame(msg_type: int, seq: int, ts: int, payload: bytes) -> bytes:
    head = struct.pack(">HBBIQH", SYNC, VERSION, msg_type, seq, ts, len(payload))
    body = head + payload
    return body + struct.pack(">H", binascii.crc_hqx(body, 0xFFFF))


def frame_entry(name: str, note: str, msg_type: int, seq: int, ts: int, payload: bytes) -> dict:
    return {
        "name": name,
        "note": note,
        "msg_type": msg_type,
        "seq": seq,
        "timestamp_ms": ts,
        "payload_hex": payload.hex(),
        "hex": frame(msg_type, seq, ts, payload).hex(),
        "expect": "accept",
    }


# ---------------------------------------------------------------- telemetry.json (ICD 6.1)

TELEMETRY_FMT = ">iiHHBBH"  # lat, lon, heading, speed, battery, mode, status_flags
TELEMETRY_FIELDS = ("lat", "lon", "heading", "speed", "battery", "mode", "status_flags")
assert struct.calcsize(TELEMETRY_FMT) == 16


def telemetry_payload(t: dict) -> bytes:
    return struct.pack(TELEMETRY_FMT, *(t[f] for f in TELEMETRY_FIELDS))


def telemetry_entry(name: str, note: str, tc: str, **fields) -> dict:
    return {
        "name": name,
        "note": note,
        "tc": tc,
        "fields": fields,
        "payload_hex": telemetry_payload(fields).hex(),
        "expect": "accept",
    }


NOMINAL = dict(lat=361234567, lon=1277654321, heading=8725, speed=450, battery=76,
               mode=1, status_flags=0b11)

TELEMETRY = [
    telemetry_entry("telemetry_nominal",
                    "ICD 7.1 예시: 36.1234567, 127.7654321, 87.25deg, 4.5m/s, 76%, MISSION, "
                    "GPS_VALID|CAMERA_OK", "TC-COM-001-01", **NOMINAL),
    telemetry_entry("telemetry_bounds_min", "모든 필드 범위 최솟값", "TC-COM-001-02",
                    lat=-900000000, lon=-1800000000, heading=0, speed=0, battery=0, mode=0,
                    status_flags=0),
    telemetry_entry("telemetry_bounds_max", "모든 필드 범위 최댓값 (정의된 플래그 4개 모두 1)",
                    "TC-COM-001-02",
                    lat=900000000, lon=1800000000, heading=35999, speed=3000, battery=100, mode=4,
                    status_flags=0x000F),
    telemetry_entry("telemetry_reserved_bits", "status_flags 예약 비트(4-15) 모두 1. 수신 측은 무시",
                    "TC-COM-001-04", **{**NOMINAL, "status_flags": 0xFFF3}),
]


def write(name: str, doc: dict) -> None:
    path = OUT_DIR / name
    path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {path}")


write("frame.json", {
    "schema": "ugvc-frame-vectors/1",
    "generator": "tools/vectors/gen_vectors.py",
    "crc_check": {"ascii": "123456789", "crc16": "29b1"},
    "vectors": [
        frame_entry("heartbeat_seq0", "HEARTBEAT, payload 없음 (최소 프레임 20B)", 0x20, 0, TS, b""),
        frame_entry("heartbeat_max_fields", "seq·timestamp 최대값: 상위 바이트 처리 확인", 0x20,
                    0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF, b""),
        frame_entry("ack_accepted", "ACK cmd_id=1 result=ACCEPTED", 0x11, 7, TS + 123,
                    struct.pack(">IB", 1, 0)),
        frame_entry("telemetry_nominal", "TELEMETRY 프레임 (payload는 telemetry.json의 telemetry_nominal)",
                    0x01, 1532, TS + 118, telemetry_payload(NOMINAL)),
    ],
})

write("telemetry.json", {
    "schema": "ugvc-telemetry-vectors/1",
    "generator": "tools/vectors/gen_vectors.py",
    "payload_size": 16,
    "vectors": TELEMETRY,
})
