import os
import queue
import socket
import subprocess
import threading
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
GATEWAY_BIN = Path(os.environ.get("UGVC_GATEWAY_BIN", REPO / "gateway" / "build" / "ugvc_gateway"))


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture
def gateway():
    """GW를 임의 포트로 실행하고 (process, vehicle_port)를 돌려준다."""
    if not GATEWAY_BIN.exists():
        pytest.skip(f"gateway not built: {GATEWAY_BIN}")
    port = free_udp_port()
    proc = subprocess.Popen([str(GATEWAY_BIN), "--vehicle-port", str(port)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    # 시작 메시지(stderr) 대기
    line = proc.stderr.readline()
    assert "listening" in line, line
    yield proc, port
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def read_lines(proc: subprocess.Popen, n: int, timeout_s: float) -> list[str]:
    """GW stdout에서 n줄을 읽는다 (시간 초과 시 읽은 만큼).

    select + readline은 쓰지 않는다: readline이 여러 줄을 내부 버퍼로 미리 읽어 가면
    select가 더 읽을 것이 없다고 판단해 멈춘다. 스레드가 줄을 큐에 넣고 여기서 꺼낸다.
    """
    q = _stdout_queue(proc)
    lines: list[str] = []
    deadline = time.monotonic() + timeout_s
    while len(lines) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        try:
            lines.append(q.get(timeout=remaining))
        except queue.Empty:
            break
    return lines


_queues: dict[int, "queue.Queue[str]"] = {}


def _stdout_queue(proc: subprocess.Popen) -> "queue.Queue[str]":
    if proc.pid not in _queues:
        q: "queue.Queue[str]" = queue.Queue()

        def pump() -> None:
            for line in proc.stdout:
                q.put(line)

        threading.Thread(target=pump, daemon=True).start()
        _queues[proc.pid] = q
    return _queues[proc.pid]
