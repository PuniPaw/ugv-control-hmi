"""차량 운동 모사 (현재: 고정 방위 직선 주행).

상태는 공학 단위(float)로 계산하고, 송신할 때만 ICD 정수 LSB로 바꾼다 (ADR-002).
경로점 주행·명령 처리·Failsafe는 이후 단계에서 추가한다.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

from ugvc_proto.telemetry import Flags, Mode, Telemetry

EARTH_M_PER_DEG_LAT = 111_320.0  # 위도 1도 ≈ 111.32 km (근거리 평면 근사)


@dataclass
class VehicleState:
    lat_deg: float
    lon_deg: float
    heading_deg: float
    speed_mps: float
    battery_pct: float
    mode: Mode = Mode.MISSION
    flags: Flags = Flags.GPS_VALID | Flags.CAMERA_OK


def step(s: VehicleState, dt: float, battery_drain_pct_per_s: float = 0.01) -> VehicleState:
    """dt초 동안 현재 방위·속도로 직진한 다음 상태 (진북 기준 시계방향)."""
    dist = s.speed_mps * dt
    h = math.radians(s.heading_deg)
    dlat = dist * math.cos(h) / EARTH_M_PER_DEG_LAT
    dlon = dist * math.sin(h) / (EARTH_M_PER_DEG_LAT * math.cos(math.radians(s.lat_deg)))
    battery = max(0.0, s.battery_pct - battery_drain_pct_per_s * dt)
    flags = s.flags | Flags.BATTERY_LOW if battery <= 20.0 else s.flags & ~Flags.BATTERY_LOW
    return VehicleState(s.lat_deg + dlat, s.lon_deg + dlon, s.heading_deg, s.speed_mps,
                        battery, s.mode, flags)


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
