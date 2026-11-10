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


# ---------------------------------------------------------------- detection.json / ack.json (ICD 6.2, 6.4)

DET_HEAD_FMT = ">IQHHB"   # frame_id, capture_ts, img_w, img_h, count
DET_OBJ_FMT = ">BHHHHH"   # class_id, confidence, x, y, w, h
assert struct.calcsize(DET_HEAD_FMT) == 17 and struct.calcsize(DET_OBJ_FMT) == 11


def detection_payload(frame_id, capture_ts, img_w, img_h, objects, count=None) -> bytes:
    n = len(objects) if count is None else count
    body = struct.pack(DET_HEAD_FMT, frame_id, capture_ts, img_w, img_h, n)
    return body + b"".join(struct.pack(DET_OBJ_FMT, *o) for o in objects)


def detection_entry(name, note, frame_id, capture_ts, img_w, img_h, objects) -> dict:
    return {
        "name": name,
        "note": note,
        "fields": {
            "frame_id": frame_id, "capture_ts": capture_ts, "img_w": img_w, "img_h": img_h,
            "objects": [dict(zip(("class_id", "confidence", "x", "y", "w", "h"), o)) for o in objects],
        },
        "payload_hex": detection_payload(frame_id, capture_ts, img_w, img_h, objects).hex(),
        "expect": "accept",
    }


DET_NOMINAL_OBJS = [(0, 9150, 100, 120, 50, 150), (1, 8000, 300, 200, 120, 80)]
DET_NOMINAL = (42, TS - 35, 640, 480, DET_NOMINAL_OBJS)
DET_MAX_OBJS = [(255, 5000, i * 10, 0, 10, 10) for i in range(32)]

DETECTION = [
    detection_entry("detection_nominal", "640x480, PERSON 0.915, VEHICLE 0.8", *DET_NOMINAL),
    detection_entry("detection_empty", "count 0 = 탐지 없음", 43, TS - 30, 640, 480, []),
    detection_entry("detection_max_count", "count 32, 최대 길이 369B", 44, TS - 25, 640, 480, DET_MAX_OBJS),
    detection_entry("detection_box_at_edge", "박스가 이미지 오른쪽·아래 끝에 딱 맞음 (x+w = img_w)", 45, TS, 640, 480,
                    [(2, 10000, 600, 400, 40, 80)]),
]


def ack_payload(cmd_id, result) -> bytes:
    return struct.pack(">IB", cmd_id, result)


ACK = [
    {"name": f"ack_result_{r}", "fields": {"cmd_id": 1000 + r, "result": r},
     "payload_hex": ack_payload(1000 + r, r).hex(), "expect": "accept"}
    for r in (0, 1, 2)
]

# ---------------------------------------------------------------- validation.json (ICD 9장 규칙 ①~⑧)
#
# 각 실패 경우는 겨냥한 규칙 하나만 어기고 나머지는 모두 맞게 만든다
# (예: sync만 틀린 프레임도 CRC는 다시 계산한다). 그래야 첫 실패 규칙이 의도한 규칙이 된다.

MSG_TELEMETRY, MSG_DETECTION, MSG_COMMAND, MSG_ACK = 0x01, 0x02, 0x10, 0x11


def raw_frame(msg_type, payload, *, sync=SYNC, version=VERSION, seq=1, ts=TS,
              payload_len=None, crc_xor=0) -> bytes:
    n = len(payload) if payload_len is None else payload_len
    body = struct.pack(">HBBIQH", sync, version, msg_type, seq, ts, n) + payload
    return body + struct.pack(">H", binascii.crc_hqx(body, 0xFFFF) ^ crc_xor)


def flip_bit(data: bytes, index: int) -> bytes:
    b = bytearray(data)
    b[index] ^= 0x01
    return bytes(b)


TEL_OK = telemetry_payload(NOMINAL)
DET_OK = detection_payload(*DET_NOMINAL)
ACK_OK = ack_payload(1, 0)


def tel(**over) -> bytes:
    return telemetry_payload({**NOMINAL, **over})


def det(img_w=640, img_h=480, objects=None, count=None) -> bytes:
    return detection_payload(42, TS, img_w, img_h, DET_NOMINAL_OBJS if objects is None else objects, count)


def case(tc, name, source, frame_bytes, expect) -> dict:
    return {"tc": tc, "name": name, "source": source, "hex": frame_bytes.hex(), "expect": expect}


V, D = "VEHICLE", "DETECTOR"
GOOD_TEL = raw_frame(MSG_TELEMETRY, TEL_OK)

VALIDATION = [
    # 기준선: 모두 수락
    case("TC-COM-002-01", "telemetry_nominal", V, GOOD_TEL, "accept"),
    case("TC-COM-002-01", "ack_accepted", V, raw_frame(MSG_ACK, ACK_OK), "accept"),
    *[case("TC-COM-002-01", d["name"], D, raw_frame(MSG_DETECTION, bytes.fromhex(d["payload_hex"])), "accept")
      for d in DETECTION],
    case("TC-COM-002-01", "telemetry_bounds_min", V, raw_frame(MSG_TELEMETRY, telemetry_payload(TELEMETRY[1]["fields"])), "accept"),
    case("TC-COM-002-01", "telemetry_bounds_max", V, raw_frame(MSG_TELEMETRY, telemetry_payload(TELEMETRY[2]["fields"])), "accept"),
    case("TC-COM-001-04", "telemetry_reserved_bits", V, raw_frame(MSG_TELEMETRY, tel(status_flags=0xFFF3)), "accept"),
    # ① 길이 < 20
    case("TC-COM-002-03", "empty_datagram", V, b"", "length"),
    case("TC-COM-002-03", "len_19", V, GOOD_TEL[:19], "length"),
    # ② sync
    case("TC-COM-002-04", "sync_swapped", V, raw_frame(MSG_TELEMETRY, TEL_OK, sync=0x55AA), "sync"),
    # ③ version
    case("TC-COM-002-05", "version_2", V, raw_frame(MSG_TELEMETRY, TEL_OK, version=0x02), "version"),
    # ④ 수신 길이 != 20 + payload_len
    case("TC-COM-002-06", "payload_len_plus_1", V, raw_frame(MSG_TELEMETRY, TEL_OK, payload_len=17), "length"),
    case("TC-COM-002-06", "payload_len_minus_1", V, raw_frame(MSG_TELEMETRY, TEL_OK, payload_len=15), "length"),
    # ⑤ CRC
    case("TC-COM-002-07", "payload_bit_flip", V, flip_bit(GOOD_TEL, 20), "crc"),
    case("TC-COM-002-07", "crc_field_flip", V, raw_frame(MSG_TELEMETRY, TEL_OK, crc_xor=0x0001), "crc"),
    # ⑥ 포트별 허용 msg_type
    case("TC-COM-002-08", "detection_on_vehicle_port", V, raw_frame(MSG_DETECTION, DET_OK), "type"),
    case("TC-COM-002-08", "telemetry_on_detector_port", D, GOOD_TEL, "type"),
    case("TC-COM-002-08", "command_inbound", V, raw_frame(MSG_COMMAND, bytes(13)), "type"),
    case("TC-COM-002-08", "undefined_type_0x99", V, raw_frame(0x99, b""), "type"),
    # ⑦ msg_type별 payload 길이
    case("TC-COM-002-09", "telemetry_n15", V, raw_frame(MSG_TELEMETRY, TEL_OK[:15]), "length"),
    case("TC-COM-002-09", "ack_n6", V, raw_frame(MSG_ACK, ACK_OK + b"\x00"), "length"),
    case("TC-COM-002-09", "detection_count2_one_object", D, raw_frame(MSG_DETECTION, det(count=2, objects=DET_NOMINAL_OBJS[:1])), "length"),
    case("TC-COM-002-09", "detection_n16", D, raw_frame(MSG_DETECTION, DET_OK[:16]), "length"),
    # ⑧ TELEMETRY 범위
    *[case("TC-COM-002-10", f"telemetry_{k}_{v}", V, raw_frame(MSG_TELEMETRY, tel(**{k: v})), "range")
      for k, v in [("lat", 900000001), ("lat", -900000001), ("lon", 1800000001), ("lon", -1800000001),
                   ("heading", 36000), ("speed", 3001), ("battery", 101), ("mode", 5)]],
    # ⑧ DETECTION 범위
    case("TC-COM-002-11", "img_w_0", D, raw_frame(MSG_DETECTION, det(img_w=0, objects=[])), "range"),
    case("TC-COM-002-11", "img_w_4097", D, raw_frame(MSG_DETECTION, det(img_w=4097, objects=[])), "range"),
    case("TC-COM-002-11", "img_h_0", D, raw_frame(MSG_DETECTION, det(img_h=0, objects=[])), "range"),
    case("TC-COM-002-11", "count_33", D, raw_frame(MSG_DETECTION, det(objects=DET_MAX_OBJS + [(255, 1, 0, 10, 1, 1)])), "range"),
    case("TC-COM-002-11", "confidence_10001", D, raw_frame(MSG_DETECTION, det(objects=[(0, 10001, 0, 0, 1, 1)])), "range"),
    case("TC-COM-002-11", "x_eq_img_w", D, raw_frame(MSG_DETECTION, det(objects=[(0, 5000, 640, 0, 1, 1)])), "range"),
    case("TC-COM-002-11", "y_eq_img_h", D, raw_frame(MSG_DETECTION, det(objects=[(0, 5000, 0, 480, 1, 1)])), "range"),
    case("TC-COM-002-11", "box_past_right_edge", D, raw_frame(MSG_DETECTION, det(objects=[(0, 5000, 600, 0, 41, 1)])), "range"),
    case("TC-COM-002-11", "box_past_bottom_edge", D, raw_frame(MSG_DETECTION, det(objects=[(0, 5000, 0, 400, 1, 81)])), "range"),
    case("TC-COM-002-11", "w_0", D, raw_frame(MSG_DETECTION, det(objects=[(0, 5000, 0, 0, 0, 1)])), "range"),
    case("TC-COM-002-11", "h_0", D, raw_frame(MSG_DETECTION, det(objects=[(0, 5000, 0, 0, 1, 0)])), "range"),
    case("TC-COM-002-11", "class_id_3_undefined", D, raw_frame(MSG_DETECTION, det(objects=[(3, 5000, 0, 0, 1, 1)])), "range"),
    # ⑧ ACK 범위
    case("TC-COM-002-12", "ack_result_3", V, raw_frame(MSG_ACK, ack_payload(1, 3)), "range"),
    # 여러 규칙 동시 위반 → 첫 규칙만
    case("TC-COM-002-13", "sync_and_crc_wrong", V, raw_frame(MSG_TELEMETRY, TEL_OK, sync=0x55AA, crc_xor=0x00FF), "sync"),
    case("TC-COM-002-13", "crc_and_range_wrong", V, raw_frame(MSG_TELEMETRY, tel(battery=200), crc_xor=0x00FF), "crc"),
]


# ---------------------------------------------------------------- command.json (ICD 6.3, GW → SIM)

COMMAND_FMT = ">IBii"  # cmd_id, cmd_type, lat, lon
assert struct.calcsize(COMMAND_FMT) == 13


def command_entry(name, note, cmd_id, cmd_type, lat=0, lon=0) -> dict:
    return {
        "name": name,
        "note": note,
        "fields": {"cmd_id": cmd_id, "cmd_type": cmd_type, "lat": lat, "lon": lon},
        "payload_hex": struct.pack(COMMAND_FMT, cmd_id, cmd_type, lat, lon).hex(),
    }


COMMAND = [
    command_entry("command_stop", "STOP, 좌표 0", 1, 0x01),
    command_entry("command_rtb", "RTB, 좌표 0", 2, 0x02),
    command_entry("command_waypoint", "WAYPOINT 36.1240000, 127.7660000", 3, 0x03, 361240000, 1277660000),
    command_entry("command_waypoint_negative", "WAYPOINT 남서반구 (음수 좌표)", 0xFFFFFFFF, 0x03, -337000000, -706000000),
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

write("detection.json", {
    "schema": "ugvc-detection-vectors/1",
    "generator": "tools/vectors/gen_vectors.py",
    "vectors": DETECTION,
})

write("ack.json", {
    "schema": "ugvc-ack-vectors/1",
    "generator": "tools/vectors/gen_vectors.py",
    "vectors": ACK,
})

write("validation.json", {
    "schema": "ugvc-validation-vectors/1",
    "generator": "tools/vectors/gen_vectors.py",
    "note": "ICD 9장 규칙 ①~⑧. expect는 accept 또는 첫 실패 규칙의 오류 계수 이름. 규칙 ⑨(seq)는 상태가 필요해 단위 시험에서 다룬다.",
    "cases": VALIDATION,
})

write("command.json", {
    "schema": "ugvc-command-vectors/1",
    "generator": "tools/vectors/gen_vectors.py",
    "vectors": COMMAND,
})
