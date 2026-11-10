import math

from ugvc_proto.telemetry import Flags, Mode, Telemetry

from ugvc_sim.vehicle import VehicleState, step, to_telemetry


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
