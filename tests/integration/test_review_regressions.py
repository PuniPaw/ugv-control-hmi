"""PR #3~#6 리뷰에서 확정된 결함의 회귀 시험."""
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from conftest import GATEWAY_BIN, REPO, free_udp_port
from ugvc_proto.frame import encode_frame
from ugvc_proto.telemetry import MSG_TELEMETRY

from ugvc_sim.telemetry_sender import START
from ugvc_sim.vehicle import to_telemetry

MSG_DETECTION = 0x02


def _need_gateway():
    if not GATEWAY_BIN.exists():
        pytest.skip(f"gateway not built: {GATEWAY_BIN}")


def test_REQ_COM_007_heartbeat_continues_when_stdout_reader_stalls():
    """stdout을 아무도 읽지 않아 파이프가 가득 차도 HEARTBEAT 1Hz는 멈추지 않는다 (SIM Failsafe 3.0s 방지)."""
    _need_gateway()
    vp, dp, sp = (free_udp_port() for _ in range(3))
    hb = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    hb.bind(("127.0.0.1", sp))
    hb.setblocking(False)
    proc = subprocess.Popen([str(GATEWAY_BIN), "--vehicle-port", str(vp), "--detector-port", str(dp),
                             "--sim-port", str(sp)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    # DETECTION 32객체 한 줄 ≈ 2.5KB × 30Hz: 1~2초 안에 파이프(64KB)가 찬다
    objects = b"".join(bytes.fromhex("00138800010001000a000a") for _ in range(32))
    arrivals = []
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            t0 = time.monotonic()
            seq = 0
            while time.monotonic() - t0 < 6.0:
                payload = (seq).to_bytes(4, "big") + bytes(8) + (640).to_bytes(2, "big") + (480).to_bytes(2, "big") + bytes([32]) + objects
                tx.sendto(encode_frame(MSG_DETECTION, seq, 0, payload), ("127.0.0.1", dp))
                seq += 1
                try:
                    while True:
                        hb.recv(64)
                        arrivals.append(time.monotonic() - t0)
                except BlockingIOError:
                    pass
                time.sleep(1 / 30)
    finally:
        proc.kill()
        hb.close()
    assert len(arrivals) >= 5, arrivals
    gaps = [b - a for a, b in zip(arrivals, arrivals[1:])]
    assert max(gaps) < 1.2, gaps


@pytest.mark.parametrize("args, message", [
    (["--vehicle-port", "70001"], "--vehicle-port must be a port number 1-65535"),
    (["--vehicle-port", "-1"], "--vehicle-port must be a port number 1-65535"),
    (["--vehicle-port", "15x"], "--vehicle-port must be a port number 1-65535"),
    (["--detector-port"], "missing value for --detector-port"),
])
def test_cli_rejects_bad_options(args, message):
    _need_gateway()
    r = subprocess.run([str(GATEWAY_BIN), *args], capture_output=True, text=True, timeout=5)
    assert r.returncode != 0
    assert message in r.stderr, r.stderr


def test_cli_sim_host_accepts_hostname():
    """--sim-host localhost: 이름을 해석해 그 주소로 HEARTBEAT를 보낸다."""
    _need_gateway()
    sp = free_udp_port()
    hb = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    hb.bind(("127.0.0.1", sp))
    hb.settimeout(3.0)
    proc = subprocess.Popen([str(GATEWAY_BIN), "--vehicle-port", str(free_udp_port()), "--detector-port",
                             str(free_udp_port()), "--sim-host", "localhost", "--sim-port", str(sp)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        data, _ = hb.recvfrom(64)
    finally:
        proc.kill()
        hb.close()
    assert data[3] == 0x20  # msg_type HEARTBEAT


def test_REQ_COM_002_oversize_datagram_counted_as_length(gateway):
    """1200B를 넘는 datagram은 규칙 ①에서 length (count 200 DETECTION도 range가 아니라 length)."""
    obj = bytes.fromhex("00138800000000000100" + "01")
    payload = (7).to_bytes(4, "big") + bytes(8) + (640).to_bytes(2, "big") + (480).to_bytes(2, "big") + bytes([200]) + obj * 200
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(encode_frame(MSG_DETECTION, 0, 0, payload), ("127.0.0.1", gateway.detector_port))
    stats = gateway.messages("stats", 1, timeout_s=3.0,
                             pred=lambda m: m["data"]["source"] == "DETECTOR" and m["data"]["errors"]["length"] == 1)
    assert stats and stats[0]["data"]["errors"]["range"] == 0


def test_stp_test_names_exist():
    """STP "테스트" 열의 이름이 모두 실제 테스트로 존재한다 (RTM grep 전제)."""
    gtest = REPO / "gateway" / "build" / "ugvc_tests"
    if not gtest.exists():
        pytest.skip(f"gtest binary not built: {gtest}")
    r = subprocess.run([sys.executable, "-I", str(REPO / "tools" / "check_stp_tests.py"), str(gtest)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
