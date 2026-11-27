"""차량 운동 모사: 경로점·귀환 주행, 정지, 명령 적용, Failsafe 진입, 남은 경로 보고.

상태는 공학 단위(float)로 계산하고, 송신할 때만 ICD 정수 LSB로 바꾼다 (ADR-002).
모든 함수는 시계·소켓 없이 상태만 받아 새 상태를 돌려주는 순수 함수다.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, replace

from ugvc_proto.command import Command, CmdType
from ugvc_proto.route import Route
from ugvc_proto.telemetry import Flags, Mode, Telemetry

EARTH_M_PER_DEG_LAT = 111_320.0  # 위도 1도 ≈ 111.32 km (근거리 평면 근사)
CRUISE_MPS = 4.5                 # 경로점·귀환 주행 속도
_LSB_DEG = 1e-7                  # COMMAND WAYPOINT 좌표 LSB (ICD 6.3)

LatLon = tuple[float, float]


@dataclass(frozen=True)
class VehicleState:
    lat_deg: float
    lon_deg: float
    heading_deg: float
    speed_mps: float
    battery_pct: float
    mode: Mode = Mode.STANDBY
    flags: Flags = Flags.GPS_VALID | Flags.CAMERA_OK
    route: tuple[LatLon, ...] = ()  # 남은 경로점 (도). 앞에서부터 방문


# 시작 상태: STANDBY로 정지, 경로 없음 (REQ-SAFE-003). 운용자 명령(WAYPOINT·RTB)이 와야 움직인다.
START = VehicleState(lat_deg=36.1234567, lon_deg=127.7654321, heading_deg=87.25,
                     speed_mps=0.0, battery_pct=76.0)
HOME: LatLon = (START.lat_deg, START.lon_deg)  # RTB 목적지 = 시작 위치


def _local_offset_m(frm: LatLon, to: LatLon) -> tuple[float, float]:
    """frm에서 to까지의 (북쪽 m, 동쪽 m). 근거리 평면 근사."""
    north = (to[0] - frm[0]) * EARTH_M_PER_DEG_LAT
    east = (to[1] - frm[1]) * EARTH_M_PER_DEG_LAT * math.cos(math.radians(frm[0]))
    return north, east


def _move(s: VehicleState, heading_deg: float, dist_m: float) -> VehicleState:
    h = math.radians(heading_deg)
    dlat = dist_m * math.cos(h) / EARTH_M_PER_DEG_LAT
    dlon = dist_m * math.sin(h) / (EARTH_M_PER_DEG_LAT * math.cos(math.radians(s.lat_deg)))
    return replace(s, lat_deg=s.lat_deg + dlat, lon_deg=s.lon_deg + dlon, heading_deg=heading_deg)


def _drive_to(s: VehicleState, target: LatLon, dist_m: float) -> tuple[VehicleState, bool]:
    """target 쪽으로 방위를 즉시 돌려 dist_m만큼 간다. 남은 거리가 dist_m 이하면 그 점에 붙고 True."""
    north, east = _local_offset_m((s.lat_deg, s.lon_deg), target)
    remaining = math.hypot(north, east)
    if remaining <= dist_m:
        heading = math.degrees(math.atan2(east, north)) % 360.0 if remaining > 0 else s.heading_deg
        return replace(s, lat_deg=target[0], lon_deg=target[1], heading_deg=heading), True
    return _move(s, math.degrees(math.atan2(east, north)) % 360.0, dist_m), False


def step(s: VehicleState, dt: float, home: LatLon = HOME,
         battery_drain_pct_per_s: float = 0.01) -> VehicleState:
    """dt초 뒤 상태 (진북 기준 시계방향 방위). STOPPED·STANDBY·FAULT는 움직이지 않고 배터리만 닳는다."""
    battery = max(0.0, s.battery_pct - battery_drain_pct_per_s * dt)
    flags = s.flags | Flags.BATTERY_LOW if battery <= 20.0 else s.flags & ~Flags.BATTERY_LOW
    s = replace(s, battery_pct=battery, flags=flags)
    dist = s.speed_mps * dt
    if s.mode == Mode.RTB:
        s, arrived = _drive_to(s, home, dist)
        return replace(s, mode=Mode.STANDBY, speed_mps=0.0) if arrived else s
    if s.mode == Mode.MISSION:
        if not s.route:  # WAYPOINT로만 MISSION에 들어오므로 생기지 않지만, 경로 없이 달리지 않는다 (REQ-SAFE-003)
            return replace(s, mode=Mode.STANDBY, speed_mps=0.0)
        s, arrived = _drive_to(s, s.route[0], dist)
        if not arrived:
            return s
        route = s.route[1:]
        return replace(s, route=route) if route else replace(s, route=(), mode=Mode.STANDBY, speed_mps=0.0)
    return s


def enter_failsafe(s: VehicleState) -> VehicleState:
    """REQ-SAFE-001: 어떤 모드에서든 정지. 이미 Failsafe면 그대로 (한 번만 전이). 진행 중이던 임무는 버린다."""
    if s.flags & Flags.FAILSAFE_ACTIVE:
        return s
    return replace(s, mode=Mode.STOPPED, speed_mps=0.0, route=(), flags=s.flags | Flags.FAILSAFE_ACTIVE)


def apply_command(s: VehicleState, cmd: Command) -> VehicleState:
    """수락한 명령을 상태에 적용하고 FAILSAFE_ACTIVE를 지운다 (REQ-SAFE-002). 링크가 살아 있을 때만 부른다.

    STOP·RTB는 진행 중인 경로를 취소한다. WAYPOINT는 경로 끝에 더한다.
    """
    s = replace(s, flags=s.flags & ~Flags.FAILSAFE_ACTIVE)
    match cmd.cmd_type:
        case CmdType.STOP:
            return replace(s, mode=Mode.STOPPED, speed_mps=0.0, route=())
        case CmdType.RTB:
            return replace(s, mode=Mode.RTB, speed_mps=CRUISE_MPS, route=())
        case CmdType.WAYPOINT:
            point = (cmd.lat * _LSB_DEG, cmd.lon * _LSB_DEG)
            return replace(s, mode=Mode.MISSION, speed_mps=CRUISE_MPS, route=(*s.route, point))
    return s


def to_telemetry(s: VehicleState) -> Telemetry:
    """공학 단위 → ICD 6.1 정수 LSB. 반올림 후 범위로 자른다."""
    def clamp(v: int, lo: int, hi: int) -> int:
        return max(lo, min(hi, v))

    return Telemetry(
        lat=clamp(round(s.lat_deg * 1e7), -900_000_000, 900_000_000),
        lon=clamp(round(s.lon_deg * 1e7), -1_800_000_000, 1_800_000_000),
        heading=round(s.heading_deg * 100) % 36000,
        speed=clamp(round(s.speed_mps * 100), 0, 3000),
        battery=clamp(round(s.battery_pct), 0, 100),
        mode=int(s.mode),
        status_flags=int(s.flags),
    )


def to_route(s: VehicleState) -> Route:
    """남은 경로 → ICD 6.6 정수 LSB. 경로점은 WAYPOINT 좌표(1e-7 deg 정수)에서 왔으므로 반올림하면 받은 값 그대로다."""
    return Route(tuple((round(lat * 1e7), round(lon * 1e7)) for lat, lon in s.route))
