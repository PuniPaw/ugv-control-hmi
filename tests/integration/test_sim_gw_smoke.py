"""SIM(Python) → GW(C++) 연결 확인: Python이 보낸 바이트를 C++가 같은 값으로 해석하는가 (ADR-002).

TC-COM-001-05(10Hz 600개 × 3회, stats 확인)의 축소판. stats 출력이 생기면 그 TC로 대체한다.
"""
import json
import socket
import time

from ugvc_proto.frame import encode_frame
from ugvc_proto.telemetry import MSG_TELEMETRY, Telemetry

from conftest import read_lines
from ugvc_sim.telemetry_sender import START
from ugvc_sim.vehicle import step, to_telemetry

N = 30


def test_sim_telemetry_decoded_identically_by_gateway(gateway):
    proc, port = gateway
    sent: list[Telemetry] = []
    state = START
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for seq in range(N):
            t = to_telemetry(state)
            sent.append(t)
            sock.sendto(encode_frame(MSG_TELEMETRY, seq, time.time_ns() // 1_000_000, t.to_payload()),
                        ("127.0.0.1", port))
            state = step(state, 0.1)
            time.sleep(0.01)

    lines = read_lines(proc, N, timeout_s=5.0)
    assert len(lines) == N
    for seq, (line, t) in enumerate(zip(lines, sent)):
        msg = json.loads(line)
        d = msg["data"]
        assert msg["type"] == "telemetry"
        assert d["seq"] == seq
        # GW는 정수 LSB × 배율로 공학 단위를 만든다 → 다시 LSB로 돌리면 송신값과 같아야 한다
        assert round(d["lat"] * 1e7) == t.lat
        assert round(d["lon"] * 1e7) == t.lon
        assert round(d["heading_deg"] * 100) == t.heading
        assert round(d["speed_mps"] * 100) == t.speed
        assert d["battery_pct"] == t.battery
        assert d["mode"] == "MISSION"
        assert d["flags"] == ["GPS_VALID", "CAMERA_OK"]


def test_gateway_rejects_corrupted_crc(gateway):
    proc, port = gateway
    good = encode_frame(MSG_TELEMETRY, 0, 0, to_telemetry(START).to_payload())
    bad = good[:-1] + bytes([good[-1] ^ 0x01])
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(bad, ("127.0.0.1", port))
        sock.sendto(good, ("127.0.0.1", port))
    lines = read_lines(proc, 1, timeout_s=3.0)
    assert len(lines) == 1 and json.loads(lines[0])["data"]["seq"] == 0
