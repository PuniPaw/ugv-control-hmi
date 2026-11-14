"""명령 중계·재전송 (REQ-CMD-002·003·005·006, ICD 6.3·6.4·7.1 cmd_result·7.2 command).

실제 GW와 실제 SIM 수신부(`ugvc_sim.uplink`)를 띄우고, Python WebSocket 클라이언트를 HMI로 써서 확인한다.
SIM은 GW의 HEARTBEAT·COMMAND 포트(`--sim-port`)에서 받고, ACK는 GW 차량 포트(`--vehicle-port`)로 보낸다.
"""
import asyncio
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest
import websockets

REPO = Path(__file__).resolve().parents[2]


def _ws_url(gateway):
    return f"ws://127.0.0.1:{gateway.ws_port}/ws"


@pytest.fixture
def sim_uplink(gateway):
    """SIM 수신부를 띄우는 함수. drop_first: 처음 받은 COMMAND N개를 무시한다 (첫 송신 유실 재현)."""
    procs = []

    def start(drop_first: int = 0, gw_port: int | None = None):
        p = subprocess.Popen(
            [sys.executable, "-m", "ugvc_sim.uplink", "--port", str(gateway.sim_port),
             "--gw-port", str(gateway.vehicle_port if gw_port is None else gw_port), "--drop-first", str(drop_first)],
            cwd=REPO / "sim", env={"PYTHONPATH": f"{REPO / 'python'}:{REPO / 'sim'}", "PATH": "/usr/bin:/bin"},
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        procs.append(p)
        time.sleep(0.3)  # 소켓 바인드
        assert p.poll() is None, p.stderr.read()
        return p

    yield start
    for p in procs:
        p.terminate()
        p.wait(timeout=5)


async def _command_and_wait(gateway, command: dict, timeout_s: float):
    """HMI로 명령 하나를 보내고 같은 req_id의 cmd_result를 기다린다. (결과, 걸린 시간 s)"""
    async with websockets.connect(_ws_url(gateway)) as ws:
        t0 = time.monotonic()
        await ws.send(json.dumps({"type": "command", "data": command}))
        deadline = t0 + timeout_s
        while time.monotonic() < deadline:
            try:
                m = json.loads(await asyncio.wait_for(ws.recv(), deadline - time.monotonic()))
            except asyncio.TimeoutError:
                break
            if m["type"] == "cmd_result" and m["data"]["req_id"] == command["req_id"]:
                return m, time.monotonic() - t0
    return None, time.monotonic() - t0


def _run(gateway, command, timeout_s):
    return asyncio.run(_command_and_wait(gateway, command, timeout_s))


def _stderr_lines(gateway, needle: str, n: int, timeout_s: float = 2.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = [line for line in gateway.stderr_lines if needle in line]
        if len(found) >= n:
            return found
        time.sleep(0.05)
    return [line for line in gateway.stderr_lines if needle in line]


def test_REQ_CMD_002_command_accepted_on_first_send(gateway, sim_uplink):
    """SIM이 바로 ACK하면 cmd_result ACCEPTED, attempts 1. stdout(기록)과 WebSocket에 같은 결과가 나간다."""
    sim_uplink()
    result, elapsed = _run(gateway, {"req_id": "r-stop", "cmd_type": "STOP"}, 3.0)
    assert result is not None
    assert result["data"]["result"] == "ACCEPTED"
    assert result["data"]["attempts"] == 1
    assert isinstance(result["data"]["cmd_id"], int) and result["data"]["cmd_id"] >= 1
    assert elapsed < 1.0, elapsed
    via_stdout = gateway.messages("cmd_result", 1, timeout_s=2.0, pred=lambda m: m["data"]["req_id"] == "r-stop")
    assert via_stdout and via_stdout[0]["data"] == result["data"]


def test_REQ_CMD_002_lost_first_send_is_retransmitted_after_1s(gateway, sim_uplink):
    """첫 COMMAND가 유실되면 1.0s 뒤 같은 cmd_id로 재전송되고, 두 번째 송신에 ACK가 와서 attempts 2."""
    sim = sim_uplink(drop_first=1)
    result, elapsed = _run(gateway, {"req_id": "r-rtb", "cmd_type": "RTB"}, 4.0)
    assert result is not None
    assert result["data"]["result"] == "ACCEPTED"
    assert result["data"]["attempts"] == 2
    assert 0.95 <= elapsed < 1.6, elapsed
    sim.terminate()
    out = sim.communicate(timeout=5)[0].splitlines()
    commands = [json.loads(line) for line in out if '"drop"' in line or '"command"' in line]
    assert [c["type"] for c in commands] == ["drop", "command"]
    assert commands[0]["cmd_id"] == commands[1]["cmd_id"] == result["data"]["cmd_id"]  # 같은 cmd_id로 재전송


def test_REQ_CMD_002_no_ack_times_out_after_three_retransmits(gateway):
    """SIM이 없으면 0·1·2·3s에 4번 보내고 4.0s에 TIMEOUT, attempts 4."""
    result, elapsed = _run(gateway, {"req_id": "r-none", "cmd_type": "STOP"}, 6.0)
    assert result is not None
    assert result["data"]["result"] == "TIMEOUT"
    assert result["data"]["attempts"] == 4
    assert 3.95 <= elapsed < 4.6, elapsed


def test_REQ_CMD_005_sends_retransmits_and_result_are_logged(gateway, sim_uplink):
    """명령 송신·재전송·결과가 모두 GW 진단 기록(stderr)에 남는다."""
    sim_uplink(drop_first=1)
    result, _ = _run(gateway, {"req_id": "r-log", "cmd_type": "STOP"}, 4.0)
    assert result is not None
    sent = _stderr_lines(gateway, "cmd sent req=\"r-log\"", 2)
    assert len(sent) == 2, gateway.stderr_lines
    assert "attempt=1" in sent[0] and "attempt=2" in sent[1]
    done = _stderr_lines(gateway, "cmd result req=\"r-log\"", 1)
    assert len(done) == 1 and "result=ACCEPTED" in done[0] and "attempts=2" in done[0]


def test_REQ_CMD_002_invalid_hmi_command_is_rejected_without_sending(gateway, sim_uplink):
    """범위 밖 WAYPOINT(위도 91)는 GW가 보내지 않고 바로 REJECTED_INVALID(cmd_id null, attempts 0)로 답한다."""
    sim = sim_uplink()
    result, elapsed = _run(gateway, {"req_id": "r-bad", "cmd_type": "WAYPOINT", "lat": 91.0, "lon": 127.0}, 2.0)
    assert result is not None
    assert result["data"] == {"req_id": "r-bad", "cmd_id": None, "result": "REJECTED_INVALID", "attempts": 0}
    assert elapsed < 0.5, elapsed
    sim.terminate()
    out = sim.communicate(timeout=5)[0]
    assert '"command"' not in out  # SIM에는 아무것도 가지 않았다


def test_REQ_CMD_002_waypoint_is_relayed(gateway, sim_uplink):
    """경로점 명령(도 단위)이 COMMAND WAYPOINT로 SIM에 전달되어 실행되고 ACCEPTED로 돌아온다."""
    sim = sim_uplink()
    result, _ = _run(gateway, {"req_id": "r-wp", "cmd_type": "WAYPOINT", "lat": 36.124, "lon": 127.766}, 3.0)
    assert result is not None and result["data"]["result"] == "ACCEPTED"
    sim.terminate()
    out = [json.loads(line) for line in sim.communicate(timeout=5)[0].splitlines() if '"command"' in line]
    assert [(c["cmd_type"], c["executed"]) for c in out] == [(3, True)]


def test_REQ_CMD_003_retransmitted_command_executes_once(gateway, sim_uplink):
    """ACK가 GW에 닿지 않으면 GW는 같은 cmd_id로 4번 보낸다. SIM은 처음 한 번만 실행하고 매번 같은 ACK를 보낸다."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as black_hole:  # ACK를 받아 버리는 곳
        black_hole.bind(("127.0.0.1", 0))
        sim = sim_uplink(gw_port=black_hole.getsockname()[1])
        result, _ = _run(gateway, {"req_id": "r-lost-ack", "cmd_type": "STOP"}, 6.0)
    assert result is not None and result["data"]["result"] == "TIMEOUT" and result["data"]["attempts"] == 4
    sim.terminate()
    out = [json.loads(line) for line in sim.communicate(timeout=5)[0].splitlines() if '"command"' in line]
    assert [c["executed"] for c in out] == [True, False, False, False]
    assert {c["cmd_id"] for c in out} == {out[0]["cmd_id"]}
    assert {c["result"] for c in out} == {"ACCEPTED"}


def test_REQ_CMD_005_req_id_cannot_forge_diagnostic_lines(gateway):
    """req_id는 HMI가 보낸 텍스트다. 개행이 들어 있어도 명령 기록 줄이 둘로 쪼개져 가짜 진단 줄이 생기지 않는다."""
    forged = "x\nlink VEHICLE LOST -> CONNECTED"
    result, _ = _run(gateway, {"req_id": forged, "cmd_type": "STOP"}, 6.0)
    assert result is not None and result["data"]["req_id"] == forged
    assert not any(line.strip() == "link VEHICLE LOST -> CONNECTED" for line in gateway.stderr_lines)
    assert _stderr_lines(gateway, 'cmd sent req="x\\nlink VEHICLE LOST -> CONNECTED"', 1)


def _sim_commands(sim) -> list[dict]:
    """SIM 수신부를 끝내고 명령 처리 기록(JSON 줄)을 돌려준다."""
    sim.terminate()
    return [json.loads(line) for line in sim.communicate(timeout=5)[0].splitlines() if '"command"' in line]


async def _collect_results(ws, req_id: str, timeout_s: float) -> list[dict]:
    """timeout_s 동안 받은 그 req_id의 cmd_result 전부."""
    out, deadline = [], time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            m = json.loads(await asyncio.wait_for(ws.recv(), deadline - time.monotonic()))
        except asyncio.TimeoutError:
            break
        if m["type"] == "cmd_result" and m["data"]["req_id"] == req_id:
            out.append(m["data"])
    return out


def test_REQ_CMD_003_restarted_gw_does_not_reuse_cmd_ids(gateway, sim_uplink):
    """SIM을 띄운 채 GW를 다시 시작해도 새 명령의 cmd_id가 앞 GW의 것과 겹치지 않아 SIM이 실행한다 (ICD 7.3)."""
    from conftest import start_gateway, stop_gateway

    sim = sim_uplink()
    first, _ = _run(gateway, {"req_id": "before", "cmd_type": "STOP"}, 3.0)
    assert first is not None and first["data"]["result"] == "ACCEPTED"
    stop_gateway(gateway)
    gw2 = start_gateway([gateway.vehicle_port, gateway.detector_port, gateway.sim_port])
    try:
        second, _ = _run(gw2, {"req_id": "after", "cmd_type": "WAYPOINT", "lat": 36.1, "lon": 127.7}, 3.0)
    finally:
        stop_gateway(gw2)
    assert second is not None and second["data"]["result"] == "ACCEPTED"
    assert second["data"]["cmd_id"] != first["data"]["cmd_id"]
    assert [(c["cmd_id"], c["executed"]) for c in _sim_commands(sim)] == [
        (first["data"]["cmd_id"], True), (second["data"]["cmd_id"], True)]


def test_REQ_CMD_002_restarted_sim_uplink_acks_are_accepted(gateway, sim_uplink):
    """텔레메트리로 차량 링크가 CONNECTED인 채 SIM 수신부만 다시 시작해 ACK seq가 0부터 시작해도 결과가 온다."""
    tel = subprocess.Popen(
        [sys.executable, "-m", "ugvc_sim.telemetry_sender", "--port", str(gateway.vehicle_port)],
        cwd=REPO / "sim", env={"PYTHONPATH": f"{REPO / 'python'}:{REPO / 'sim'}", "PATH": "/usr/bin:/bin"},
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        sim = sim_uplink()
        for i in range(3):
            r, _ = _run(gateway, {"req_id": f"a{i}", "cmd_type": "STOP"}, 3.0)
            assert r is not None and r["data"]["result"] == "ACCEPTED"
        _sim_commands(sim)
        sim_uplink()  # 다시 시작: ACK seq 0부터
        r, _ = _run(gateway, {"req_id": "after-restart", "cmd_type": "STOP"}, 3.0)
        assert r is not None and r["data"]["result"] == "ACCEPTED" and r["data"]["attempts"] == 1
    finally:
        tel.terminate()
        tel.wait(timeout=5)


def test_REQ_CMD_002_invalid_command_with_pending_req_id_does_not_give_a_second_result(gateway, sim_uplink):
    """대기 중인 req_id로 형식이 틀린 명령이 와도 그 req_id의 결과는 하나뿐이다 (ICD 7.2)."""
    sim_uplink(drop_first=1)  # 첫 송신을 잃어 1초 동안 대기 중

    async def run():
        async with websockets.connect(_ws_url(gateway)) as ws:
            await ws.send(json.dumps({"type": "command", "data": {"req_id": "op-2", "cmd_type": "RTB"}}))
            await asyncio.sleep(0.3)
            await ws.send(json.dumps({"type": "command", "data": {"req_id": "op-2", "cmd_type": "BOGUS"}}))
            return await _collect_results(ws, "op-2", 2.5)

    results = asyncio.run(run())
    assert [(r["result"], r["attempts"]) for r in results] == [("ACCEPTED", 2)]
    assert _stderr_lines(gateway, 'cmd ignored req="op-2" reason=invalid command with a req_id in use', 1)


def test_REQ_CMD_002_same_req_id_from_another_hmi_is_not_sent_and_both_hmis_get_the_result(gateway, sim_uplink):
    """HMI A의 명령이 대기 중일 때 HMI B가 같은 req_id로 다른 명령을 보내면 보내지 않고 기록한다.
    결과는 모든 HMI에 간다 (ICD 7.1)."""
    sim = sim_uplink(drop_first=1)

    async def run():
        async with websockets.connect(_ws_url(gateway)) as a, websockets.connect(_ws_url(gateway)) as b:
            await a.send(json.dumps({"type": "command", "data": {"req_id": "1", "cmd_type": "STOP"}}))
            await asyncio.sleep(0.3)
            await b.send(json.dumps({"type": "command", "data": {"req_id": "1", "cmd_type": "WAYPOINT", "lat": 1, "lon": 1}}))
            return await asyncio.gather(_collect_results(a, "1", 2.5), _collect_results(b, "1", 2.5))

    got_a, got_b = asyncio.run(run())
    assert [r["result"] for r in got_a] == ["ACCEPTED"]
    assert got_b == got_a
    assert [c["cmd_type"] for c in _sim_commands(sim)] == [1]  # WAYPOINT는 SIM에 가지 않았다
    assert _stderr_lines(gateway, 'cmd ignored req="1" reason=req_id pending with different command', 1)


def test_REQ_CMD_003_hmi_resending_after_missing_the_result_does_not_execute_twice(gateway, sim_uplink):
    """결과를 놓친 HMI가 다시 접속해 같은 req_id·같은 명령을 보내면 다시 실행하지 않고 같은 결과를 다시 받는다."""
    sim = sim_uplink()
    cmd = {"req_id": "wp-7", "cmd_type": "WAYPOINT", "lat": 36.124, "lon": 127.766}
    first, _ = _run(gateway, cmd, 3.0)
    again, elapsed = _run(gateway, cmd, 3.0)
    assert first is not None and again is not None
    assert again["data"] == first["data"]
    assert elapsed < 0.5, elapsed
    assert [c["executed"] for c in _sim_commands(sim)] == [True]
    assert _stderr_lines(gateway, 'cmd result req="wp-7"', 2)[-1].rstrip().endswith("(repeated)")
