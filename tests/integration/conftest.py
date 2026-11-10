import json
import os
import queue
import socket
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GATEWAY_BIN = Path(os.environ.get("UGVC_GATEWAY_BIN", REPO / "gateway" / "build" / "ugvc_gateway"))


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


@pytest.fixture
def gateway():
    if not GATEWAY_BIN.exists():
        pytest.skip(f"gateway not built: {GATEWAY_BIN}")
    ports = [free_udp_port() for _ in range(3)]
    proc = subprocess.Popen(
        [str(GATEWAY_BIN), "--vehicle-port", str(ports[0]), "--detector-port", str(ports[1]),
         "--sim-port", str(ports[2])],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    line = proc.stderr.readline()
    assert "ugvc_gateway" in line, line
    gw = GatewayProc(proc, *ports)
    gw.start_pump()
    yield gw
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
