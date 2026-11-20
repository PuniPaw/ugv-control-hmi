import math
from dataclasses import replace

from ugvc_proto.telemetry import Flags, Mode, Telemetry

from ugvc_proto.command import CmdType, Command

from ugvc_sim.vehicle import HOME, START, VehicleState, apply_command, step, to_telemetry


def _state(**kw) -> VehicleState:
    base = dict(lat_deg=36.0, lon_deg=127.0, heading_deg=0.0, speed_mps=10.0, battery_pct=50.0)
    return VehicleState(**{**base, **kw})


def test_north_moves_lat_only():
    s = step(_state(heading_deg=0.0), dt=1.0)
    assert math.isclose((s.lat_deg - 36.0) * 111_320.0, 10.0, rel_tol=1e-9)
    assert s.lon_deg == 127.0


def test_east_moves_lon_only():
    s = step(_state(heading_deg=90.0), dt=1.0)
    assert abs(s.lat_deg - 36.0) < 1e-12
    assert s.lon_deg > 127.0


def test_battery_low_flag_at_20_percent():
    s = step(_state(battery_pct=20.005), dt=1.0)
    assert s.battery_pct <= 20.0
    assert s.flags & Flags.BATTERY_LOW


def test_to_telemetry_uses_icd_lsb():
    t = to_telemetry(VehicleState(36.1234567, 127.7654321, 87.25, 4.5, 76.0))
    assert t == Telemetry(lat=361234567, lon=1277654321, heading=8725, speed=450, battery=76,
                          mode=Mode.MISSION, status_flags=Flags.GPS_VALID | Flags.CAMERA_OK)


def test_to_telemetry_clamps_and_wraps():
    t = to_telemetry(_state(heading_deg=359.999, speed_mps=99.0, battery_pct=120.0))
    assert t.heading == 0      # 36000 → 0 (0~35999)
    assert t.speed == 3000
    assert t.battery == 100


# 명령이 차량 운동에 미치는 영향. SIM 운동 모사의 정확도라 REQ ID를 달지 않는다 (STP 2.7 예외)

NEAR_LAT, NEAR_LON = 361240000, 1277660000  # START 근처 경로점 (≈ 60 m, 북동쪽), 1e-7 deg


def _drive(s, seconds, dt=0.1):
    for _ in range(round(seconds / dt)):
        s = step(s, dt)
    return s


def test_stop_sets_speed_zero_and_stays():
    s = apply_command(START, Command(1, CmdType.STOP))
    assert s.mode == Mode.STOPPED and s.speed_mps == 0
    after = _drive(s, 5.0)
    assert (after.lat_deg, after.lon_deg) == (s.lat_deg, s.lon_deg)


def test_waypoint_route_arrival_goes_standby():
    s = apply_command(START, Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON))
    s = apply_command(s, Command(2, CmdType.WAYPOINT, NEAR_LAT + 10_000, NEAR_LON))
    assert s.mode == Mode.MISSION and len(s.route) == 2
    s = _drive(s, 120.0)
    assert s.mode == Mode.STANDBY and s.speed_mps == 0 and s.route == ()
    tel = to_telemetry(s)
    assert (tel.lat, tel.lon) == (NEAR_LAT + 10_000, NEAR_LON)


def test_waypoint_heading_points_at_first_route_point():
    s = step(apply_command(START, Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON)), 0.1)
    assert 0.0 < s.heading_deg < 90.0  # 북동쪽 점


def test_rtb_returns_home_then_standby():
    s = apply_command(replace(START, lat_deg=START.lat_deg + 0.001), Command(1, CmdType.RTB))
    assert s.mode == Mode.RTB and s.speed_mps > 0
    s = _drive(s, 120.0)
    assert (s.lat_deg, s.lon_deg) == HOME and s.mode == Mode.STANDBY and s.speed_mps == 0


def test_standby_does_not_move_but_battery_drains():
    s = replace(START, mode=Mode.STANDBY, speed_mps=0.0)
    after = _drive(s, 10.0)
    assert (after.lat_deg, after.lon_deg) == (s.lat_deg, s.lon_deg) and after.battery_pct < s.battery_pct


def test_start_state_keeps_fixed_heading_leg():
    s = _drive(START, 2.0)
    tel = to_telemetry(s)
    assert tel.heading == 8725 and tel.mode == Mode.MISSION and tel.speed == 450
    assert (s.lat_deg, s.lon_deg) != (START.lat_deg, START.lon_deg)
