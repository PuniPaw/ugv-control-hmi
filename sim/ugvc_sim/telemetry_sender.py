"""TELEMETRY를 GW(IF-01a, UDP 15001)로 주기 송신한다.

사용:
    python3 -m ugvc_sim.telemetry_sender                # 10Hz, 무한
    python3 -m ugvc_sim.telemetry_sender --count 600    # 600개(60초) 후 종료
"""
from __future__ import annotations

import argparse
import socket
import sys
import time

from ugvc_proto.frame import MsgType, encode_frame

from ugvc_sim.vehicle import VehicleState, step, to_telemetry

START = VehicleState(lat_deg=36.1234567, lon_deg=127.7654321, heading_deg=87.25,
                     speed_mps=4.5, battery_pct=76.0)


def epoch_ms() -> int:
    return time.time_ns() // 1_000_000


def run(host: str, port: int, rate_hz: float, count: int | None, verbose: bool) -> int:
    period = 1.0 / rate_hz
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    state = START
    seq = 0
    t0 = time.monotonic()
    try:
        while count is None or seq < count:
            frame = encode_frame(MsgType.TELEMETRY, seq & 0xFFFFFFFF, epoch_ms(),
                                 to_telemetry(state).to_payload())
            sock.sendto(frame, (host, port))
            if verbose:
                print(f"seq={seq} {frame.hex()}", flush=True)
            seq += 1
            state = step(state, period)
            # 절대 시각 기준 스케줄: sleep 오차가 누적되지 않는다
            delay = t0 + seq * period - time.monotonic()
            if delay > 0:
                time.sleep(delay)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
    print(f"sent {seq} TELEMETRY to {host}:{port}", file=sys.stderr)
    return seq


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=15001)
    p.add_argument("--rate", type=float, default=10.0, help="Hz (기본 10)")
    p.add_argument("--count", type=int, default=None, help="보낼 개수 (기본: 무한)")
    p.add_argument("-v", "--verbose", action="store_true", help="보낸 프레임 hex 출력")
    a = p.parse_args(argv)
    run(a.host, a.port, a.rate, a.count, a.verbose)


if __name__ == "__main__":
    main()
