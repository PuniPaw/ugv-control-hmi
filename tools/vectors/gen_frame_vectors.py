#!/usr/bin/env python3
"""testdata/vectors/frame.json 생성기.

C++(gateway)·Python(ugvc_proto) 구현과 독립된 기준을 만들기 위해
표준 라이브러리(struct, binascii.crc_hqx)만 사용한다. 우리 codec을 import하지 않는다.

binascii.crc_hqx(data, 0xFFFF)는 CRC-16/CCITT-FALSE와 같다 (poly 0x1021, init 0xFFFF, 반사 없음).

사용: python3 tools/vectors/gen_frame_vectors.py  (저장소 루트에서)
"""
import binascii
import json
import struct
from pathlib import Path

OUT = Path(__file__).resolve().parents[2] / "testdata" / "vectors" / "frame.json"

SYNC, VERSION = 0xAA55, 0x01
TS = 1794729600000  # ICD 7.1 예시 기준 시각


def frame(msg_type: int, seq: int, ts: int, payload: bytes) -> bytes:
    head = struct.pack(">HBBIQH", SYNC, VERSION, msg_type, seq, ts, len(payload))
    body = head + payload
    return body + struct.pack(">H", binascii.crc_hqx(body, 0xFFFF))


def entry(name: str, note: str, msg_type: int, seq: int, ts: int, payload: bytes) -> dict:
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


VECTORS = [
    entry("heartbeat_seq0", "HEARTBEAT, payload 없음 (최소 프레임 20B)", 0x20, 0, TS, b""),
    entry("heartbeat_max_fields", "seq·timestamp 최대값: 상위 바이트 처리 확인", 0x20,
          0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF, b""),
    entry("ack_accepted", "ACK cmd_id=1 result=ACCEPTED", 0x11, 7, TS + 123,
          struct.pack(">IB", 1, 0)),
    entry("telemetry_nominal",
          "ICD 7.1 예시 값: lat 36.1234567, lon 127.7654321, heading 87.25, speed 4.5, "
          "battery 76, MISSION, GPS_VALID|CAMERA_OK",
          0x01, 1532, TS + 118,
          struct.pack(">iiHHBBH", 361234567, 1277654321, 8725, 450, 76, 1, 0b11)),
]

doc = {
    "schema": "ugvc-frame-vectors/1",
    "generator": "tools/vectors/gen_frame_vectors.py",
    "crc_check": {"ascii": "123456789", "crc16": "29b1"},
    "vectors": VECTORS,
}
OUT.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"wrote {OUT} ({len(VECTORS)} vectors)")
