"""남은 경로 보고·중계와 STANDBY 기동 (REQ-CMD-007, REQ-SAFE-003, ICD 6.6·7.1 route).

실제 GW와 실제 SIM(`python -m ugvc_sim`)을 띄우고, Python WebSocket 클라이언트를 HMI로 써서 확인한다.
"""
import asyncio
import json
import time

import websockets

from conftest import ws_url

NEAR = (36.124, 127.766)  # SIM 시작 위치(36.1234567, 127.7654321) 근처 (≈ 60 m)


async def _recv_until(ws, pred, timeout_s: float) -> list[dict]:
    """pred(지금까지 받은 메시지 목록)가 참이 되거나 시간이 다 될 때까지 받은 메시지 전부."""
    out, deadline = [], time.monotonic() + timeout_s
    while time.monotonic() < deadline and not pred(out):
        try:
            out.append(json.loads(await asyncio.wait_for(ws.recv(), deadline - time.monotonic())))
        except asyncio.TimeoutError:
            break
    return out


def _of(msgs: list[dict], type_: str) -> list[dict]:
    return [m["data"] for m in msgs if m["type"] == type_]


def _waypoint(req_id: str, lat: float, lon: float) -> str:
    return json.dumps({"type": "command", "data": {"req_id": req_id, "cmd_type": "WAYPOINT", "lat": lat, "lon": lon}})


def _result(msgs: list[dict], req_id: str) -> dict | None:
    return next((d for d in _of(msgs, "cmd_result") if d["req_id"] == req_id), None)


def test_REQ_CMD_007_route_relayed_from_sim_to_hmi(gateway, sim_uplink):
    """SIM이 1Hz로 보내는 빈 경로가 HMI에 route로 온다. WAYPOINT를 수락하면 결과와 함께 1초를 기다리지 않고 새 경로가 오고,
    seq는 ROUTE 스트림의 것이다. route는 접속 직후 상태로 보내지 않는다(ICD 7.1)."""
    sim_uplink()
    time.sleep(1.2)  # SIM의 첫 ROUTE들이 GW를 지나간 뒤 접속

    async def run():
        async with websockets.connect(ws_url(gateway)) as ws:
            t_open = time.monotonic()
            first = await _recv_until(ws, lambda ms: _of(ms, "route"), 2.0)
            t_first = time.monotonic() - t_open
            await ws.send(_waypoint("wp-1", *NEAR))
            after = await _recv_until(ws, lambda ms: _result(ms, "wp-1") and any(
                d["waypoints"] for d in _of(ms, "route")), 2.0)
            return first, t_first, after

    first, t_first, after = asyncio.run(run())
    snapshot = [m["type"] for m in first[:4]]
    assert snapshot == ["stats", "link_status", "stats", "link_status"], snapshot  # 접속 직후 상태에 route 없음
    empty = _of(first, "route")
    # SIM은 1Hz로 보내므로 접속 뒤 1.0초 안에 온다. 0.5초는 시험 PC 부하로 GW·수신이 늦는 몫의 여유
    assert empty and empty[0]["waypoints"] == [] and 0.0 < t_first <= 1.5, t_first
    assert _result(after, "wp-1")["result"] == "ACCEPTED"
    route = next(d for d in _of(after, "route") if d["waypoints"])
    assert route["waypoints"] == [{"lat": NEAR[0], "lon": NEAR[1]}]
    # 1Hz를 기다리지 않는다: SIM은 ACK 바로 뒤에 ROUTE를 보내므로 GW 수신 시각(같은 GW 시계)이 거의 같다.
    # HMI 쪽 경과 시간이 아니라 GW 시각 차로 재서 시험 PC 부하에 덜 흔들린다. 정확한 시점은 TC-CMD-007-10(가짜 시계)이 본다
    result_ts = next(m["ts"] for m in after if m["type"] == "cmd_result" and m["data"]["req_id"] == "wp-1")
    assert 0 <= route["rx_ts"] - result_ts < 300, route["rx_ts"] - result_ts
    assert route["seq"] > empty[0]["seq"] and isinstance(route["rx_ts"], int)
    stats = gateway.messages("stats", 1, timeout_s=2.0, pred=lambda m: m["data"]["source"] == "VEHICLE")
    assert stats and stats[0]["data"]["lost_count"] == 0 and not any(stats[0]["data"]["errors"].values())


def test_REQ_CMD_007_full_route_rejects_waypoint_with_rejected_state(gateway, sim_uplink):
    """남은 경로가 32개면 33번째 WAYPOINT는 cmd_result REJECTED_STATE이고 route는 32개 그대로다."""
    sim_uplink()
    points = [((361_240_000 + i * 1000) / 1e7, NEAR[1]) for i in range(33)]  # 1e-7 deg 정수에서 (GW 값과 같은 double)

    async def run():
        async with websockets.connect(ws_url(gateway)) as ws:
            for i, p in enumerate(points[:32]):
                await ws.send(_waypoint(f"wp-{i}", *p))
            filled = await _recv_until(ws, lambda ms: all(_result(ms, f"wp-{i}") for i in range(32)), 6.0)
            await ws.send(_waypoint("wp-32", *points[32]))
            rest = await _recv_until(ws, lambda ms: _result(ms, "wp-32") and len(_of(ms, "route")) >= 2, 3.0)
            return filled, rest

    filled, rest = asyncio.run(run())
    assert [_result(filled, f"wp-{i}")["result"] for i in range(32)] == ["ACCEPTED"] * 32
    assert _result(rest, "wp-32")["result"] == "REJECTED_STATE"
    routes = _of(rest, "route")
    assert routes and all(len(d["waypoints"]) == 32 for d in routes)
    # 첫 점을 향해 주행 중이라 도착 전까지 32개 그대로, 순서는 보낸 순서
    assert [(w["lat"], w["lon"]) for w in routes[-1]["waypoints"]] == points[:32]


def test_REQ_SAFE_003_sim_starts_standby_and_does_not_move_without_command(gateway, sim_uplink):
    """SIM을 띄우고 GW의 HEARTBEAT가 이어지는 3초 동안 명령이 없으면 telemetry는 모두 STANDBY·speed 0·같은 위치."""
    sim_uplink()

    async def run():
        async with websockets.connect(ws_url(gateway)) as ws:
            return await _recv_until(ws, lambda ms: False, 3.5)

    tel = _of(asyncio.run(run()), "telemetry")
    assert len(tel) >= 30
    assert all(d["mode"] == "STANDBY" and d["speed_mps"] == 0 and "FAILSAFE_ACTIVE" not in d["flags"] for d in tel)
    assert len({(d["lat"], d["lon"]) for d in tel}) == 1
