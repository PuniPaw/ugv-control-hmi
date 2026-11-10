"""STP 4장 통합 TC. 오래 걸리므로 slow 표시: cd tests && python3 -m pytest -m slow"""
import socket
import time

import pytest

from ugvc_proto.frame import FRAME_OVERHEAD, crc16_ccitt_false, decode_header, encode_frame, read_frame_crc
from ugvc_proto.telemetry import MSG_TELEMETRY

from ugvc_sim.telemetry_sender import START
from ugvc_sim.vehicle import step, to_telemetry

MSG_HEARTBEAT = 0x20


def _send_10hz(port: int, count: int) -> None:
    period = 0.1
    state = START
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        t0 = time.monotonic()
        for seq in range(count):
            sock.sendto(encode_frame(MSG_TELEMETRY, seq, time.time_ns() // 1_000_000,
                                     to_telemetry(state).to_payload()), ("127.0.0.1", port))
            state = step(state, period)
            delay = t0 + (seq + 1) * period - time.monotonic()
            if delay > 0:
                time.sleep(delay)


@pytest.mark.slow
@pytest.mark.parametrize("run", [1, 2, 3])
def test_REQ_COM_001_10hz_no_loss(gateway, run):
    """TC-COM-001-05: 10Hz TELEMETRY 600개(60초), 3회. 매 회 rx_count 600, 유실 0, 오류 0."""
    _send_10hz(gateway.vehicle_port, 600)
    final = gateway.messages("stats", 1, timeout_s=3.0,
                             pred=lambda m: m["data"]["source"] == "VEHICLE" and m["data"]["rx_count"] == 600)
    assert final, "rx_count가 600에 도달하지 않음"
    d = final[0]["data"]
    assert d["lost_count"] == 0
    assert all(v == 0 for v in d["errors"].values()), d["errors"]


@pytest.mark.slow
def test_REQ_COM_007_heartbeat_interval(gateway):
    """TC-COM-007-03: 모의 SIM이 HEARTBEAT 31개를 받아 간격 30개가 모두 1000 ± 100ms."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", gateway.sim_port))
    sock.settimeout(3.0)
    arrivals = []
    try:
        while len(arrivals) < 31:
            data, _ = sock.recvfrom(2048)
            arrivals.append(time.monotonic())
            h = decode_header(data)
            assert h is not None and h.msg_type == MSG_HEARTBEAT and h.payload_len == 0
            assert len(data) == FRAME_OVERHEAD
            assert read_frame_crc(data) == crc16_ccitt_false(data[:-2])
    finally:
        sock.close()
    intervals_ms = [(b - a) * 1000 for a, b in zip(arrivals, arrivals[1:])]
    assert all(900 <= x <= 1100 for x in intervals_ms), intervals_ms
