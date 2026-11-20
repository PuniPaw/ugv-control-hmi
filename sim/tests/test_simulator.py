"""Simulator(순수 로직) 시험. 시계는 명시적 `now` 값이다 (sleep 없음)."""
from dataclasses import replace

from ugvc_proto.command import Ack, AckResult, CmdType, Command
from ugvc_proto.telemetry import Flags, Mode

from ugvc_sim.simulator import Simulator
from ugvc_sim.uplink import InvalidCommand, ReceivedCommand
from ugvc_sim.vehicle import HOME, START

NEAR_LAT, NEAR_LON = 361240000, 1277660000  # START(36.1234567, 127.7654321) 근처 경로점 (≈ 60 m)


def _cmd(cmd_id, cmd_type, lat=0, lon=0):
    return ReceivedCommand(0, 0, Command(cmd_id, cmd_type, lat, lon))


def _every_second(t):
    return round(t * 10) % 10 == 0


def _run(sim, t_from, t_to, hb=None, dt=0.1):
    """t_from < t ≤ t_to를 dt 간격으로 advance한다. hb(t)가 참이면 그 tick 직전에 HEARTBEAT를 준다. t_to를 돌려준다."""
    n = round((t_to - t_from) / dt)
    for i in range(1, n + 1):
        t = round(t_from + i * dt, 6)
        if hb and hb(t):
            sim.on_heartbeat(t)
        sim.advance(t, dt)
    return t_to


def _pos(sim):
    return (sim.state.lat_deg, sim.state.lon_deg)


def _failsafe_telemetry(sim):
    t = sim.telemetry()
    return t.mode == Mode.STOPPED and t.speed == 0 and bool(t.status_flags & Flags.FAILSAFE_ACTIVE)


def _alive_sim(command=None, now=0.0):
    """HEARTBEAT를 막 받은 링크 살아 있는 Simulator. 필요하면 명령 하나를 먼저 처리한다."""
    sim = Simulator(start=0.0)
    sim.on_heartbeat(now)
    if command is not None:
        sim.on_command(command, now)
    return sim


def test_REQ_SAFE_001_no_heartbeat_since_start_failsafe_at_3s():
    sim = Simulator(start=0.0)
    sim.advance(2.999, 0.1)
    t = sim.telemetry()
    assert t.mode == Mode.MISSION and t.speed > 0 and not t.status_flags & Flags.FAILSAFE_ACTIVE
    sim.advance(3.000, 0.1)
    assert _failsafe_telemetry(sim)


def test_REQ_SAFE_001_failsafe_3s_after_last_heartbeat():
    sim = Simulator(start=10.0)
    sim.on_heartbeat(10.0)
    sim.advance(12.999, 0.1)
    assert not sim.failsafe and sim.telemetry().mode == Mode.MISSION
    sim.advance(13.000, 0.1)
    assert _failsafe_telemetry(sim)


def test_REQ_SAFE_001_failsafe_from_any_mode():
    wp = _cmd(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON)
    for name, command, mode_before in [
        ("MISSION", None, Mode.MISSION),
        ("MISSION_route", wp, Mode.MISSION),
        ("STOPPED", _cmd(1, CmdType.STOP), Mode.STOPPED),
    ]:
        sim = _alive_sim(command)
        assert sim.telemetry().mode == mode_before, name
        sim.advance(3.0, 0.1)
        assert _failsafe_telemetry(sim), name
    # RTB: 집에서 떨어진 곳에서 귀환 중
    sim = _alive_sim()
    sim.state = replace(sim.state, lat_deg=START.lat_deg + 0.01)
    sim.on_command(_cmd(1, CmdType.RTB), 0.0)
    assert sim.telemetry().mode == Mode.RTB
    sim.advance(3.0, 0.1)
    assert _failsafe_telemetry(sim)
    # STANDBY: 경로점(시작 위치)에 도착해 대기
    sim = _alive_sim(_cmd(1, CmdType.WAYPOINT, int(START.lat_deg * 1e7), int(START.lon_deg * 1e7)))
    sim.advance(0.1, 0.1)
    assert sim.telemetry().mode == Mode.STANDBY
    sim.advance(3.0, 0.1)
    assert _failsafe_telemetry(sim)


def test_REQ_SAFE_001_vehicle_does_not_move_in_failsafe():
    sim = Simulator(start=0.0)
    _run(sim, 0.0, 3.0)
    assert _failsafe_telemetry(sim)
    frozen = _pos(sim)
    _run(sim, 3.0, 10.0)
    assert _pos(sim) == frozen


def test_REQ_SAFE_001_failsafe_is_a_single_transition_per_outage():
    sim = Simulator(start=0.0)
    _run(sim, 0.0, 3.0)
    before = sim.state
    _run(sim, 3.0, 6.0)
    assert sim.state.mode == before.mode and sim.state.flags & Flags.FAILSAFE_ACTIVE
    assert sim.state.route == before.route


def test_REQ_SAFE_001_failsafe_again_after_second_outage():
    sim = Simulator(start=0.0)
    t = _run(sim, 0.0, 1.0, hb=lambda t: True)
    sim.on_command(_cmd(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON), t)  # 링크 살아 있음: 수락, 주행
    assert sim.telemetry().mode == Mode.MISSION and not sim.failsafe
    t = _run(sim, t, t + 0.5, hb=lambda t: True)
    t = _run(sim, t, t + 3.0)                                          # 두 번째 두절
    assert _failsafe_telemetry(sim)
    frozen = _pos(sim)
    _run(sim, t, t + 2.0)
    assert _pos(sim) == frozen


def test_REQ_SAFE_002_heartbeat_recovery_does_not_resume():
    sim = Simulator(start=0.0)
    t = _run(sim, 0.0, 3.0)
    frozen = _pos(sim)
    _run(sim, t, t + 10.0, hb=_every_second)  # 1Hz HEARTBEAT 복귀
    assert _failsafe_telemetry(sim)
    assert _pos(sim) == frozen


def test_REQ_SAFE_002_heartbeat_arriving_after_3s_gap_still_enters_failsafe():
    """tick 없이 HEARTBEAT가 3.0초 만에 처음 도착해도 먼저 Failsafe로 들어간 뒤 감시를 되살린다 (사건 순서 무관)."""
    sim = Simulator(start=0.0)
    sim.on_heartbeat(3.0)
    assert _failsafe_telemetry(sim)


def test_REQ_SAFE_002_motion_command_after_recovery_resumes_and_clears_flag():
    for cmd_type, lat, lon, mode in [(CmdType.RTB, 0, 0, Mode.RTB),
                                     (CmdType.WAYPOINT, NEAR_LAT, NEAR_LON, Mode.MISSION)]:
        sim = Simulator(start=0.0)
        t = _run(sim, 0.0, 3.0)
        t = _run(sim, t, t + 2.0, hb=lambda x: True)
        sim.state = replace(sim.state, lat_deg=START.lat_deg + 0.01)  # 집에서 떨어진 곳 (RTB가 바로 도착하지 않도록)
        displaced = _pos(sim)
        out = sim.on_command(_cmd(1, cmd_type, lat, lon), t)
        assert out.ack == Ack(1, AckResult.ACCEPTED) and out.execute is not None
        _run(sim, t, t + 1.0, hb=lambda x: True)
        tel = sim.telemetry()
        assert tel.mode == mode and tel.speed > 0 and not tel.status_flags & Flags.FAILSAFE_ACTIVE
        assert _pos(sim) != displaced


def test_REQ_SAFE_002_stop_after_recovery_clears_flag_stays_stopped():
    sim = Simulator(start=0.0)
    t = _run(sim, 0.0, 3.0)
    t = _run(sim, t, t + 2.0, hb=lambda x: True)
    out = sim.on_command(_cmd(1, CmdType.STOP), t)
    assert out.ack == Ack(1, AckResult.ACCEPTED)
    tel = sim.telemetry()
    assert tel.mode == Mode.STOPPED and tel.speed == 0 and not tel.status_flags & Flags.FAILSAFE_ACTIVE
    frozen = _pos(sim)
    _run(sim, t, t + 2.0, hb=lambda x: True)
    assert _pos(sim) == frozen and sim.telemetry().mode == Mode.STOPPED


def test_REQ_SAFE_002_motion_command_during_outage_rejected_state():
    sim = Simulator(start=0.0)
    t = _run(sim, 0.0, 3.5)
    frozen = _pos(sim)
    for i, (cmd_type, lat, lon) in enumerate([(CmdType.RTB, 0, 0), (CmdType.WAYPOINT, NEAR_LAT, NEAR_LON)], start=1):
        out = sim.on_command(_cmd(i, cmd_type, lat, lon), t)
        assert out.ack == Ack(i, AckResult.REJECTED_STATE) and out.execute is None
    t = _run(sim, t, t + 1.0)
    assert _failsafe_telemetry(sim) and _pos(sim) == frozen
    out = sim.on_command(_cmd(3, CmdType.STOP), t)  # STOP은 수락하되 Failsafe는 그대로
    assert out.ack == Ack(3, AckResult.ACCEPTED)
    assert _failsafe_telemetry(sim) and _pos(sim) == frozen


def test_REQ_SAFE_002_rejected_state_cmd_id_retransmitted_after_recovery_keeps_result():
    sim = Simulator(start=0.0)
    t = _run(sim, 0.0, 3.5)
    assert sim.on_command(_cmd(7, CmdType.RTB), t).ack == Ack(7, AckResult.REJECTED_STATE)
    sim.on_heartbeat(t)  # 링크 복구
    out = sim.on_command(_cmd(7, CmdType.RTB), t + 0.1)
    assert out.ack == Ack(7, AckResult.REJECTED_STATE) and out.execute is None
    assert _failsafe_telemetry(sim)
    assert sim.on_command(_cmd(8, CmdType.RTB), t + 0.2).ack == Ack(8, AckResult.ACCEPTED)  # 새 cmd_id는 실행


def test_REQ_SAFE_002_invalid_command_during_outage_is_rejected_invalid():
    sim = Simulator(start=0.0)
    t = _run(sim, 0.0, 3.5)
    out = sim.on_command(InvalidCommand(0, 0, Command(5, 0x99)), t)
    assert out.ack == Ack(5, AckResult.REJECTED_INVALID) and out.execute is None


# 아래는 명령이 차량에 미치는 영향 (REQ ID 없이 모사 동작만 확인)

def test_stop_sets_speed_zero():
    sim = _alive_sim(_cmd(1, CmdType.STOP))
    t = sim.telemetry()
    assert t.mode == Mode.STOPPED and t.speed == 0


def test_waypoint_route_arrival_goes_standby():
    sim = _alive_sim(_cmd(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON))
    sim.on_command(_cmd(2, CmdType.WAYPOINT, NEAR_LAT + 10_000, NEAR_LON), 0.0)  # 두 번째 점
    assert len(sim.state.route) == 2
    _run(sim, 0.0, 120.0, hb=_every_second)
    tel = sim.telemetry()
    assert tel.mode == Mode.STANDBY and tel.speed == 0 and sim.state.route == ()
    assert (tel.lat, tel.lon) == (NEAR_LAT + 10_000, NEAR_LON)
    assert not tel.status_flags & Flags.FAILSAFE_ACTIVE


def test_waypoint_heading_points_at_first_route_point():
    sim = _alive_sim(_cmd(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON))
    sim.advance(0.1, 0.1)
    assert 0.0 < sim.telemetry().heading / 100 < 90.0  # 북동쪽 점


def test_rtb_returns_home_then_standby():
    sim = _alive_sim()
    sim.state = replace(sim.state, lat_deg=START.lat_deg + 0.001)
    sim.on_command(_cmd(1, CmdType.RTB), 0.0)
    _run(sim, 0.0, 120.0, hb=_every_second)
    assert _pos(sim) == HOME
    assert sim.telemetry().mode == Mode.STANDBY and sim.telemetry().speed == 0


def test_standby_does_not_move_but_battery_drains():
    sim = _alive_sim(_cmd(1, CmdType.RTB))  # 시작 위치가 집이라 첫 step에 도착해 STANDBY
    _run(sim, 0.0, 1.0, hb=_every_second)
    assert sim.telemetry().mode == Mode.STANDBY
    pos, battery = _pos(sim), sim.state.battery_pct
    _run(sim, 1.0, 11.0, hb=_every_second)
    assert _pos(sim) == pos and sim.state.battery_pct < battery


def test_start_state_keeps_fixed_heading_leg():
    sim = _alive_sim()
    _run(sim, 0.0, 2.0, hb=lambda t: True)
    tel = sim.telemetry()
    assert tel.heading == 8725 and tel.mode == Mode.MISSION and tel.speed == 450
