#!/usr/bin/env python3
"""실제 GW를 실행해 IF-03 JSON Lines 표본(gw_if03_sample.jsonl)을 다시 만든다.

GW의 IF-03 형식(ICD 7.1, gateway/src/if03)을 바꾸면 이 스크립트를 다시 돌리고 vitest를 실행한다.
표본에 들어가는 것: 접속 직후 상태(stats·link_status), TELEMETRY 2개(위치 유효, GPS_VALID 없음),
DETECTION 1개(객체 2개), 1Hz stats, 링크 전이.

사용 (저장소 루트에서): PYTHONPATH=python python3 hmi/tests/fixtures/capture_gw_if03.py
"""
import os
import socket
import subprocess
import time
from pathlib import Path

from ugvc_proto.frame import MsgType, encode_frame
from ugvc_proto.telemetry import Flags, Mode, Telemetry

ROOT = Path(__file__).resolve().parents[3]
GW = Path(os.environ.get("UGVC_GATEWAY_BIN", ROOT / "gateway" / "build" / "ugvc_gateway"))
OUT = Path(__file__).with_name("gw_if03_sample.jsonl")


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def detection_payload(frame_id: int) -> bytes:
    # 객체: class_id u8, conf u16(1e-4), x, y, w, h u16 (ICD 6.2)
    objs = [(0, 5000, 1, 1, 10, 10), (1, 9123, 100, 50, 80, 60)]
    body = b"".join(bytes([c]) + v.to_bytes(2, "big") + b"".join(n.to_bytes(2, "big") for n in box)
                    for c, v, *box in objs)
    return (frame_id.to_bytes(4, "big") + (1794442520000).to_bytes(8, "big")
            + (640).to_bytes(2, "big") + (480).to_bytes(2, "big") + bytes([len(objs)]) + body)


def main() -> None:
    vport, dport, sport = free_port(), free_port(), free_port()
    gw = subprocess.Popen([str(GW), "--vehicle-port", str(vport), "--detector-port", str(dport),
                           "--sim-port", str(sport), "--ws-port", "0"],
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        time.sleep(0.3)
        valid = Telemetry(lat=361234567, lon=1277654321, heading=8725, speed=450, battery=76,
                          mode=Mode.MISSION, status_flags=Flags.GPS_VALID | Flags.CAMERA_OK)
        no_fix = Telemetry(lat=0, lon=0, heading=8725, speed=450, battery=18,
                           mode=Mode.RTB, status_flags=Flags.CAMERA_OK | Flags.BATTERY_LOW)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            tx.sendto(encode_frame(MsgType.TELEMETRY, 0, 0, valid.to_payload()), ("127.0.0.1", vport))
            tx.sendto(encode_frame(MsgType.TELEMETRY, 1, 0, no_fix.to_payload()), ("127.0.0.1", vport))
            tx.sendto(encode_frame(MsgType.DETECTION, 0, 0, detection_payload(7)), ("127.0.0.1", dport))
        time.sleep(1.3)
    finally:
        gw.terminate()
    lines = gw.communicate(timeout=5)[0].splitlines()
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    types = sorted({l.split('"type":"')[1].split('"')[0] for l in lines})
    print(f"{OUT.name}: {len(lines)}줄, type {types}")


if __name__ == "__main__":
    main()
