"""IF-03 WebSocket 중계 (REQ-COM-008, ICD 7장). 실제 GW를 띄우고 Python WebSocket 클라이언트로 확인한다."""
import asyncio
import json
import os
import socket
import threading
import time

import pytest
import websockets

from ugvc_proto.frame import MsgType, encode_frame

from ugvc_sim.telemetry_sender import START
from ugvc_sim.vehicle import step, to_telemetry


def _ws_url(gateway, path="/ws"):
    return f"ws://127.0.0.1:{gateway.ws_port}{path}"


def _send_telemetry(port: int, seqs) -> None:
    state = START
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for seq in seqs:
            sock.sendto(encode_frame(MsgType.TELEMETRY, seq, 0, to_telemetry(state).to_payload()), ("127.0.0.1", port))
            state = step(state, 0.1)
            time.sleep(0.005)


def _detection_payload(seq: int, objects: int = 32) -> bytes:
    obj = bytes.fromhex("00138800010001000a000a")  # PERSON, conf 0.5, (1,1) 10×10
    return seq.to_bytes(4, "big") + bytes(8) + (640).to_bytes(2, "big") + (480).to_bytes(2, "big") + bytes([objects]) + obj * objects


async def _collect(ws, type_, n, timeout_s):
    out = []
    deadline = time.monotonic() + timeout_s
    while len(out) < n and time.monotonic() < deadline:
        try:
            m = json.loads(await asyncio.wait_for(ws.recv(), deadline - time.monotonic()))
        except asyncio.TimeoutError:
            break
        if m["type"] == type_:
            out.append(m)
    return out


def test_REQ_COM_008_ws_relays_same_messages_as_stdout(gateway):
    """WebSocket으로 받은 telemetry가 stdout JSON Lines와 같다 (같은 메시지를 두 곳에 발행)."""
    async def run():
        async with websockets.connect(_ws_url(gateway)) as ws:
            _send_telemetry(gateway.vehicle_port, range(5))
            return await _collect(ws, "telemetry", 5, 3.0)

    via_ws = asyncio.run(run())
    via_stdout = gateway.messages("telemetry", 5, timeout_s=3.0)
    assert len(via_ws) == 5
    assert via_ws == via_stdout


def test_REQ_COM_008_new_session_gets_current_link_status(gateway):
    """새 HMI는 다음 1Hz 보고를 기다리지 않고 접속 직후 송신원별 link_status·stats를 받는다."""
    _send_telemetry(gateway.vehicle_port, [0])
    gateway.messages("telemetry", 1, timeout_s=2.0)  # GW가 패킷을 처리한 뒤 접속

    async def run():
        async with websockets.connect(_ws_url(gateway)) as ws:
            t0 = time.monotonic()
            first = [json.loads(await asyncio.wait_for(ws.recv(), 1.0)) for _ in range(4)]
            return first, time.monotonic() - t0

    first, elapsed = asyncio.run(run())
    assert elapsed < 0.3, elapsed
    link = {m["data"]["source"]: m["data"]["state"] for m in first if m["type"] == "link_status"}
    assert link == {"VEHICLE": "CONNECTED", "DETECTOR": "LOST"}
    assert {m["data"]["source"] for m in first if m["type"] == "stats"} == {"VEHICLE", "DETECTOR"}


def test_REQ_COM_008_all_sessions_receive(gateway):
    async def run():
        async with websockets.connect(_ws_url(gateway)) as a, websockets.connect(_ws_url(gateway)) as b:
            _send_telemetry(gateway.vehicle_port, range(3))
            return await asyncio.gather(_collect(a, "telemetry", 3, 3.0), _collect(b, "telemetry", 3, 3.0))

    got_a, got_b = asyncio.run(run())
    assert [m["data"]["seq"] for m in got_a] == [0, 1, 2]
    assert got_a == got_b


def test_REQ_COM_008_other_path_rejected(gateway):
    async def run():
        async with websockets.connect(_ws_url(gateway, "/other")):
            pass

    with pytest.raises(websockets.exceptions.InvalidStatus) as e:
        asyncio.run(run())
    assert e.value.response.status_code == 404


def test_REQ_COM_008_hmi_message_does_not_break_session(gateway):
    """HMI 명령(IF-03 7.2)은 아직 기록만 한다. 보내도 세션은 유지되고 다음 메시지를 받는다."""
    async def run():
        async with websockets.connect(_ws_url(gateway)) as ws:
            await ws.send(json.dumps({"type": "command", "data": {"req_id": "r1", "cmd_type": "STOP"}}))
            return await _collect(ws, "stats", 3, 2.5)

    assert len(asyncio.run(run())) >= 3


def _stalled_ws_client(port: int) -> socket.socket:
    """핸드셰이크만 하고 이후 한 바이트도 읽지 않는 클라이언트 (수신 버퍼를 작게)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    s.connect(("127.0.0.1", port))
    s.sendall(b"GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")
    head = b""
    while b"\r\n\r\n" not in head:
        head += s.recv(1)  # 응답 헤더까지만 한 바이트씩 읽는다
    assert head.startswith(b"HTTP/1.1 101"), head
    return s


class _HeartbeatRecorder:
    """모의 SIM: 별도 스레드가 blocking recv로 HEARTBEAT 도착 시각을 기록한다 (시험 본문이 바빠도 정확)."""

    def __init__(self, port: int):
        self.arrivals: list[float] = []
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", port))
        self._sock.settimeout(0.2)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            try:
                self._sock.recv(64)
                self.arrivals.append(time.monotonic())
            except socket.timeout:
                pass

    def stop(self) -> list[float]:
        self._stop.set()
        self._thread.join()
        self._sock.close()
        return [b - a for a, b in zip(self.arrivals, self.arrivals[1:])]


def test_REQ_COM_008_stalled_session_is_closed_without_blocking_others(gateway):
    """읽지 않는 HMI는 대기열(1000개)이 넘치면 끊긴다. 그동안 HEARTBEAT 1Hz와 다른 HMI 수신은 계속된다.

    대기열을 실제로 넘기려면 TCP 버퍼(루프백에서 약 1400개 분량)와 세션 대기열을 모두 채워야 해서
    DETECTION 32객체(약 2.5KB)를 6000개 보낸다.
    """
    stalled = _stalled_ws_client(gateway.ws_port)
    hb = _HeartbeatRecorder(gateway.sim_port)

    async def run():
        t0 = time.monotonic()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            for seq in range(6000):
                tx.sendto(encode_frame(MsgType.DETECTION, seq, 0, _detection_payload(seq)),
                          ("127.0.0.1", gateway.detector_port))
                if seq % 50 == 0:
                    await asyncio.sleep(0.01)
        async with websockets.connect(_ws_url(gateway)) as healthy:
            _send_telemetry(gateway.vehicle_port, range(20))
            got = await _collect(healthy, "telemetry", 20, 3.0)
        while time.monotonic() - t0 < 5.5:
            await asyncio.sleep(0.05)
        return got

    try:
        got = asyncio.run(run())
        stalled.settimeout(2.0)
        closed_by_gw = _drained_to_eof(stalled)
    finally:
        stalled.close()
        gaps = hb.stop()
    assert gateway.proc.poll() is None, "GW exited"
    assert [m["data"]["seq"] for m in got] == list(range(20))
    assert len(gaps) >= 4 and max(gaps) < 1.2, gaps
    assert closed_by_gw, "stalled session was not closed"
    time.sleep(1.1)  # 끊김 보고는 1Hz
    assert any("ws clients too slow: closed" in line for line in gateway.stderr_lines), gateway.stderr_lines[-5:]


def _drained_to_eof(sock: socket.socket) -> bool:
    """쌓인 데이터를 읽어 내고, 서버가 연결을 닫았으면 True (EOF 또는 RST)."""
    try:
        while True:
            if not sock.recv(65536):
                return True
    except ConnectionResetError:
        return True
    except socket.timeout:
        return False


def test_REQ_COM_008_hmi_text_cannot_forge_diagnostics(gateway):
    """HMI 텍스트의 개행으로 진단 줄(링크 전이)을 위조할 수 없다. 받은 텍스트는 JSON 문자열로 기록된다."""
    forged = "link VEHICLE LOST -> CONNECTED"

    async def run():
        async with websockets.connect(_ws_url(gateway)) as ws:
            await ws.send("x\n" + forged)
            await _collect(ws, "stats", 1, 2.0)

    asyncio.run(run())
    time.sleep(0.3)
    lines = [l.rstrip("\n") for l in gateway.stderr_lines]
    assert forged not in lines, "forged diagnostic line"
    assert any("ws message ignored" in l and '"x\\nlink VEHICLE' in l for l in lines), lines[-3:]


def test_REQ_COM_008_path_with_query_accepted(gateway):
    async def run():
        async with websockets.connect(_ws_url(gateway, "/ws?client=hmi")) as ws:
            return await _collect(ws, "link_status", 1, 1.0)

    assert len(asyncio.run(run())) == 1


def test_REQ_COM_008_session_limit(gateway):
    """동시 세션은 8개까지. 9번째 업그레이드 요청은 503."""
    async def run():
        sessions = [await websockets.connect(_ws_url(gateway)) for _ in range(8)]
        try:
            with pytest.raises(websockets.exceptions.InvalidStatus) as e:
                await websockets.connect(_ws_url(gateway))
            return e.value.response.status_code
        finally:
            for ws in sessions:
                await ws.close()

    assert asyncio.run(run()) == 503


def test_REQ_COM_008_session_limit_concurrent(gateway):
    """16개가 동시에 접속해도 열리는 세션은 8개 이하, 나머지는 503 (핸드셰이크 중인 연결도 상한에 센다)."""
    async def run():
        results = await asyncio.gather(*(websockets.connect(_ws_url(gateway)) for _ in range(16)),
                                       return_exceptions=True)
        opened = [r for r in results if not isinstance(r, Exception)]
        refused = [r for r in results if isinstance(r, websockets.exceptions.InvalidStatus)]
        for ws in opened:
            await ws.close()
        return len(opened), [r.response.status_code for r in refused]

    for _ in range(5):  # 경쟁 조건이라 여러 번
        opened, refused = asyncio.run(run())
        assert opened <= 8, f"{opened} sessions open, cap is 8"
        assert opened + len(refused) == 16 and set(refused) <= {503}, (opened, refused)
        time.sleep(0.2)  # 닫힌 세션이 GW에서 정리될 때까지


def test_REQ_COM_008_stalled_hmi_closed_within_lag_limit(gateway):
    """운용 조건(기본 소켓 버퍼, TELEMETRY 10Hz)에서 읽지 않는 HMI는 지연 상한(2s) 안에 끊긴다.
    TCP 버퍼가 수 MB를 받아 둘 수 있어 대기열 상한만으로는 수십 분 걸린다 (PR #16 2차 리뷰).
    듣기만 하는 정상 HMI는 WebSocket 구현이 ping에 pong으로 답하므로 끊기지 않는다."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", gateway.ws_port))
    s.sendall(b"GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")
    head = b""
    while b"\r\n\r\n" not in head:
        head += s.recv(1)

    async def run():
        # 정상 HMI: 아무것도 보내지 않고 듣기만 하되, 브라우저 onmessage처럼 받는 대로 계속 소비한다.
        # (소비하지 않으면 클라이언트 라이브러리가 소켓 읽기를 멈춰 ping을 못 보고, 그 HMI도 뒤처진 것으로 끊긴다)
        async with websockets.connect(_ws_url(gateway)) as listener:
            reader = asyncio.ensure_future(_collect(listener, "telemetry", 60, 8.0))
            state = START
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
                for seq in range(60):  # 6초, 10Hz
                    tx.sendto(encode_frame(MsgType.TELEMETRY, seq, 0, to_telemetry(state).to_payload()),
                              ("127.0.0.1", gateway.vehicle_port))
                    state = step(state, 0.1)
                    await asyncio.sleep(0.1)
            got = await reader
            # 정상 HMI가 닫히기 전에 확인한다: 읽지 않던 HMI의 소켓을 끝까지 읽어 GW가 닫았는지(EOF)
            s.settimeout(1.0)
            stalled_closed = _drained_to_eof(s)
            return len(got), listener.state.name, stalled_closed

    try:
        received, listener_state, stalled_closed = asyncio.run(run())
    finally:
        s.close()
    assert stalled_closed, "stalled HMI was not closed by GW within 6s"
    assert listener_state == "OPEN" and received == 60, (listener_state, received)


def test_REQ_COM_008_lag_limit_timing(gateway):
    """읽지 않는 HMI가 끊기는 시각: 2.0s 상한, 판정 여유 0.5s."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", gateway.ws_port))
    s.sendall(b"GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")
    head = b""
    while b"\r\n\r\n" not in head:
        head += s.recv(1)
    t_open = time.monotonic()
    # 읽기를 멈춘 척: 아무것도 읽지 않다가, 서버가 닫았는지만 확인한다 (MSG_PEEK로 데이터를 꺼내지 않는다)
    s.settimeout(0.05)
    closed_at = None
    while time.monotonic() - t_open < 5.0 and closed_at is None:
        try:
            if s.recv(1, socket.MSG_PEEK) == b"":
                closed_at = time.monotonic() - t_open
        except socket.timeout:
            pass
        except ConnectionResetError:
            closed_at = time.monotonic() - t_open
        if closed_at is None and any("ws close" in l for l in gateway.stderr_lines):
            closed_at = time.monotonic() - t_open
    s.close()
    assert closed_at is not None, "not closed within 5s"
    assert 1.5 <= closed_at <= 2.5, closed_at


def test_REQ_COM_008_accept_error_backs_off():
    """fd가 바닥나 accept가 실패해도 즉시 재시도로 CPU를 태우지 않고, 오류를 기록하며, HEARTBEAT는 계속된다."""
    import resource
    import subprocess
    from conftest import GATEWAY_BIN, GW_TEST_ARGS, free_udp_port, ws_port_from_banner

    if not GATEWAY_BIN.exists():
        pytest.skip(f"gateway not built: {GATEWAY_BIN}")
    vp, dp, sp = (free_udp_port() for _ in range(3))
    proc = subprocess.Popen(
        [str(GATEWAY_BIN), "--vehicle-port", str(vp), "--detector-port", str(dp), "--sim-port", str(sp), *GW_TEST_ARGS],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
        preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_NOFILE, (40, 40)))
    hb = _HeartbeatRecorder(sp)
    clients = []
    try:
        port = ws_port_from_banner(proc.stderr.readline())
        errors = []
        threading.Thread(target=lambda: errors.extend(l for l in proc.stderr if "ws accept error" in l), daemon=True).start()
        for _ in range(60):  # fd 40개를 넘겨 accept가 EMFILE로 실패하게 한다
            c = socket.socket()
            c.connect(("127.0.0.1", port))
            clients.append(c)
        time.sleep(0.5)
        def cpu_seconds():
            fields = open(f"/proc/{proc.pid}/stat").read().rsplit(")", 1)[1].split()
            return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
        c0 = cpu_seconds()
        time.sleep(2.0)
        cpu = (cpu_seconds() - c0) / 2.0
    finally:
        for c in clients:
            c.close()
        gaps = hb.stop()
        proc.terminate()
        proc.wait(timeout=5)
    assert cpu < 0.3, f"GW CPU {cpu:.0%} while accept keeps failing"
    assert errors, "accept error not logged"
    assert gaps and max(gaps) < 1.2, gaps
