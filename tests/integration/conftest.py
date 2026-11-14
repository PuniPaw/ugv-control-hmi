import json
import os
import queue
import re
import socket
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GATEWAY_BIN = Path(os.environ.get("UGVC_GATEWAY_BIN", REPO / "gateway" / "build" / "ugvc_gateway"))
# IF-03 WebSocket은 OS가 고른 빈 포트로 연다 (기본 8080은 시험끼리·개발 중인 GW와 충돌한다).
# 실제 포트는 시작 메시지 "IF-03 ws://127.0.0.1:<port>/ws"에서 읽는다.
GW_TEST_ARGS = ["--ws-port", "0"]


def ws_port_from_banner(line: str) -> int:
    m = re.search(r"ws://[^:]+:(\d+)/ws", line)
    assert m, f"no IF-03 port in banner: {line!r}"
    return int(m.group(1))


def pytest_configure(config):
    config.addinivalue_line("markers", "slow: 30초 이상 걸리는 STP 통합 TC (-m slow로 실행)")


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@dataclass
class GatewayProc:
    """실행 중인 GW. stdout의 IF-03 JSON Lines를 스레드가 큐에 모은다.

    select + readline은 쓰지 않는다: readline이 여러 줄을 내부 버퍼로 미리 읽어 가면
    select가 더 읽을 것이 없다고 판단해 멈춘다.
    """

    proc: subprocess.Popen
    vehicle_port: int
    detector_port: int
    sim_port: int
    ws_port: int = 0
    stderr_lines: list = field(default_factory=list)  # 진단 줄 (fixture가 계속 읽어 모은다)
    _q: "queue.Queue[dict]" = field(default_factory=queue.Queue)

    def start_pump(self) -> None:
        def pump() -> None:
            for line in self.proc.stdout:
                self._q.put(json.loads(line))

        threading.Thread(target=pump, daemon=True).start()

    def messages(self, type_: str, n: int, timeout_s: float, pred=lambda m: True) -> list[dict]:
        """type이 같고 pred를 만족하는 메시지 n개 (시간 초과 시 모인 만큼). 다른 메시지는 버린다."""
        out: list[dict] = []
        deadline = time.monotonic() + timeout_s
        while len(out) < n:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                m = self._q.get(timeout=remaining)
            except queue.Empty:
                break
            if m["type"] == type_ and pred(m):
                out.append(m)
        return out

    def drain(self) -> None:
        while not self._q.empty():
            self._q.get_nowait()


def start_gateway(ports: list[int]) -> GatewayProc:
    """GW를 띄우고 stdout·stderr 수집을 시작한다. ports = [차량, 탐지, SIM]. GW 재시작 시험에서도 쓴다."""
    proc = subprocess.Popen(
        [str(GATEWAY_BIN), "--vehicle-port", str(ports[0]), "--detector-port", str(ports[1]),
         "--sim-port", str(ports[2]), *GW_TEST_ARGS],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    line = proc.stderr.readline()
    assert "ugvc_gateway" in line, line
    gw = GatewayProc(proc, *ports, ws_port=ws_port_from_banner(line))
    # stderr도 계속 비운다: 쌓여서 파이프가 차면 GW의 stderr 쓰기가 블록된다. 시험이 볼 수 있게 모아 둔다
    threading.Thread(target=lambda: gw.stderr_lines.extend(proc.stderr), daemon=True).start()
    gw.start_pump()
    return gw


def stop_gateway(gw: GatewayProc) -> None:
    gw.proc.terminate()
    try:
        gw.proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        gw.proc.kill()


@pytest.fixture
def gateway():
    if not GATEWAY_BIN.exists():
        pytest.skip(f"gateway not built: {GATEWAY_BIN}")
    gw = start_gateway([free_udp_port() for _ in range(3)])
    yield gw
    stop_gateway(gw)
