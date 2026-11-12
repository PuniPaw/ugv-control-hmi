"""PR #3~#6 리뷰에서 확정된 결함의 회귀 시험."""
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

from conftest import GATEWAY_BIN, GW_TEST_ARGS, REPO, free_udp_port
from ugvc_proto.frame import MsgType, encode_frame

from ugvc_sim.telemetry_sender import START
from ugvc_sim.vehicle import to_telemetry


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
    proc = subprocess.Popen([str(GATEWAY_BIN), *GW_TEST_ARGS, "--vehicle-port", str(vp), "--detector-port", str(dp),
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
                tx.sendto(encode_frame(MsgType.DETECTION, seq, 0, payload), ("127.0.0.1", dp))
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
    proc = subprocess.Popen([str(GATEWAY_BIN), *GW_TEST_ARGS, "--vehicle-port", str(free_udp_port()), "--detector-port",
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
        sock.sendto(encode_frame(MsgType.DETECTION, 0, 0, payload), ("127.0.0.1", gateway.detector_port))
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


# --- 출력 경로 회귀 (interrogate 2026-11-11) ---------------------------------------------
# stdout·stderr를 읽는 쪽이 멈추거나 사라져도 판정·HEARTBEAT가 멈추지 않고, 진단이 사라지지 않아야 한다.

def _full_pipe() -> tuple[int, int]:
    """이미 가득 찬 파이프 (r, w). 채울 때만 O_NONBLOCK을 쓰고, GW에 넘기기 전에 되돌린다."""
    r, w = os.pipe()
    os.set_blocking(w, False)
    try:
        while True:
            os.write(w, b"#" * 4096)
    except BlockingIOError:
        pass
    os.set_blocking(w, True)
    return r, w


def _spawn(ports, stdout, stderr):
    vp, dp, sp = ports
    return subprocess.Popen([str(GATEWAY_BIN), *GW_TEST_ARGS, "--vehicle-port", str(vp), "--detector-port", str(dp),
                             "--sim-port", str(sp)], stdout=stdout, stderr=stderr)


def _telemetry(seq: int) -> bytes:
    return encode_frame(MsgType.TELEMETRY, seq, 0, to_telemetry(START).to_payload())


def _heartbeats(sock: socket.socket, seconds: float) -> list[float]:
    t0 = time.monotonic()
    arrivals = []
    sock.settimeout(0.1)
    while time.monotonic() - t0 < seconds:
        try:
            sock.recv(64)
            arrivals.append(time.monotonic() - t0)
        except socket.timeout:
            pass
    return arrivals


def test_REQ_COM_007_heartbeat_continues_when_stderr_reader_stalls():
    """stderr 파이프가 가득 차 있어도(읽는 쪽 멈춤) HEARTBEAT 1Hz는 계속된다."""
    _need_gateway()
    ports = tuple(free_udp_port() for _ in range(3))
    r, w = _full_pipe()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as hb:
        hb.bind(("127.0.0.1", ports[2]))
        proc = _spawn(ports, subprocess.DEVNULL, w)
        os.close(w)
        try:
            arrivals = _heartbeats(hb, 4.0)
        finally:
            proc.kill()
            proc.wait()
            os.close(r)
    assert len(arrivals) >= 3, arrivals


def _fd_flags(pid: int, fd: int) -> int:
    for line in Path(f"/proc/{pid}/fdinfo/{fd}").read_text().splitlines():
        if line.startswith("flags:"):
            return int(line.split()[1], 8)
    raise AssertionError("no flags line")


def test_diagnostics_survive_shared_pipe_stall():
    """`2>&1 | reader`에서 reader가 멈췄다 돌아와도 링크 전이·버림 보고가 stderr로 나온다.

    GW는 상속받은 fd의 열린 파일 설명(open file description)에 O_NONBLOCK을 걸면 안 된다.
    stdout과 stderr가 같은 설명을 공유하므로, 걸면 std::cerr가 EAGAIN을 받아 그 뒤로 영구히 침묵한다.
    """
    _need_gateway()
    ports = tuple(free_udp_port() for _ in range(3))
    r, w = os.pipe()  # 빈 파이프에서 시작해 GW 출력으로 가득 채운다
    proc = _spawn(ports, w, w)
    os.close(w)
    lines: list[str] = []
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            # 멈춘 동안: 텔레메트리 1500개(대기열 1000줄 초과) → CONNECTED, 그 뒤 1.5s 침묵 → LOST
            for seq in range(1500):
                tx.sendto(_telemetry(seq), ("127.0.0.1", ports[0]))
                if seq % 100 == 0:
                    time.sleep(0.01)
            time.sleep(1.5)
            assert not _fd_flags(proc.pid, 1) & os.O_NONBLOCK
            assert not _fd_flags(proc.pid, 2) & os.O_NONBLOCK

            reader = os.fdopen(r, "r", errors="replace")
            pump = threading.Thread(target=lambda: lines.extend(reader), daemon=True)
            pump.start()
            time.sleep(1.5)
            # 다시 읽기 시작한 뒤의 전이: LOST -> CONNECTED
            tx.sendto(_telemetry(5000), ("127.0.0.1", ports[0]))
            time.sleep(1.5)
    finally:
        proc.kill()
        proc.wait()
    text = "".join(lines)
    assert "stdout reader is behind: dropped" in text
    assert text.count("link VEHICLE LOST -> CONNECTED") == 2, [l for l in lines if l.startswith("link")]


def test_dropped_lines_reported_after_stdout_reader_closes():
    """stdout을 읽던 쪽이 닫혀도(EPIPE) 버린 줄을 세어 stderr로 알린다."""
    _need_gateway()
    ports = tuple(free_udp_port() for _ in range(3))
    proc = _spawn(ports, subprocess.PIPE, subprocess.PIPE)
    try:
        assert "ugvc_gateway" in proc.stderr.readline().decode()
        proc.stdout.close()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            for seq in range(50):
                tx.sendto(_telemetry(seq), ("127.0.0.1", ports[0]))
        time.sleep(2.5)
    finally:
        proc.terminate()
        _, err = proc.communicate(timeout=5)
    assert "stdout reader is behind: dropped" in err.decode(), err
