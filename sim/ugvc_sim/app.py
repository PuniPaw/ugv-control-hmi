"""SIM 실행 본체: UDP 소켓 하나에서 COMMAND·HEARTBEAT를 받고 10Hz로 TELEMETRY를 보낸다 (단일 스레드).

사용:
    python3 -m ugvc_sim                    # UDP 15010 수신, TELEMETRY·ACK는 GW 127.0.0.1:15001로, 10Hz 무한
    python3 -m ugvc_sim --count 600 -v     # TELEMETRY 600개 뒤 종료, 보낸 프레임 hex 출력

표준출력은 JSON 한 줄씩: command / heartbeat / reject / drop, 그리고 모드·Failsafe 전이(mode / failsafe).
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import threading
import time
from collections.abc import Callable
from dataclasses import asdict

from ugvc_proto.command import AckResult
from ugvc_proto.frame import MsgType, Reject, encode_frame
from ugvc_proto.telemetry import Mode

from ugvc_sim.simulator import Simulator
from ugvc_sim.uplink import Heartbeat, parse_uplink

GW_VEHICLE_PORT = 15001  # IF-01a. GW는 TELEMETRY·ACK를 이 포트로 받는다
_MAX_WAIT_S = 0.1        # 소켓을 기다리는 최대 시간 (stop 확인 주기)


def _emit(obj: dict) -> None:
    print(json.dumps(obj), flush=True)


def _epoch_ms() -> int:
    return time.time_ns() // 1_000_000


def _report_transitions(sim: Simulator, before: tuple[int, bool]) -> None:
    mode, failsafe = int(sim.state.mode), sim.failsafe
    if failsafe != before[1]:
        _emit({"type": "failsafe", "active": failsafe, "mode": Mode(mode).name})
    if mode != before[0]:
        _emit({"type": "mode", "mode": Mode(mode).name})


def run(sock: socket.socket, gw_addr: tuple[str, int], rate_hz: float = 10.0, count: int | None = None,
        drop_first: int = 0, verbose: bool = False, stop: threading.Event | None = None,
        clock: Callable[[], float] = time.monotonic) -> int:
    """바인드된 소켓으로 SIM을 돌린다. 보낸 TELEMETRY 개수를 돌려준다.

    `stop`이 켜지거나 `count`개를 보내면 끝난다 (운영은 Ctrl-C).
    drop_first: 처음 받은 유효 COMMAND N개를 무시한다 (ACK·실행 없음). GW 재전송 시험용이며 기본 0.
    """
    period = 1.0 / rate_hz
    t0 = clock()
    sim = Simulator(start=t0)
    tel_seq = ack_seq = sent = 0
    to_drop = drop_first
    while (stop is None or not stop.is_set()) and (count is None or sent < count):
        now = clock()
        due = t0 + sent * period  # 절대 시각 스케줄: 지연이 누적되지 않는다
        if now >= due:
            before = (int(sim.state.mode), sim.failsafe)
            sim.advance(now, period if sent else 0.0)  # 첫 TELEMETRY는 시작 상태 그대로
            _report_transitions(sim, before)
            frame = encode_frame(MsgType.TELEMETRY, tel_seq, _epoch_ms(), sim.telemetry().to_payload())
            sock.sendto(frame, gw_addr)
            if verbose:
                print(f"seq={tel_seq} {frame.hex()}", flush=True)
            tel_seq = (tel_seq + 1) & 0xFFFFFFFF
            sent += 1
            continue
        sock.settimeout(max(0.001, min(due - now, _MAX_WAIT_S)))
        try:
            data, _ = sock.recvfrom(2048)
        except socket.timeout:
            continue
        now = clock()
        before = (int(sim.state.mode), sim.failsafe)
        msg = parse_uplink(data)
        if isinstance(msg, Reject):
            _emit({"type": "reject", "reason": msg.value, "len": len(data)})
        elif isinstance(msg, Heartbeat):
            sim.on_heartbeat(now)
            _emit({"type": "heartbeat", **asdict(msg)})
        elif to_drop > 0:
            to_drop -= 1
            _emit({"type": "drop", "cmd_id": msg.command.cmd_id})
        else:
            outcome = sim.on_command(msg, now)
            sock.sendto(encode_frame(MsgType.ACK, ack_seq, _epoch_ms(), outcome.ack.to_payload()), gw_addr)
            ack_seq = (ack_seq + 1) & 0xFFFFFFFF
            _emit({"type": "command", "cmd_id": msg.command.cmd_id, "cmd_type": msg.command.cmd_type,
                   "result": AckResult(outcome.ack.result).name, "executed": outcome.execute is not None})
        _report_transitions(sim, before)
    return sent


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host", default="127.0.0.1", help="COMMAND·HEARTBEAT를 받을 주소")
    p.add_argument("--port", type=int, default=15010, help="수신 포트 (IF-01b)")
    p.add_argument("--gw-host", default="127.0.0.1", help="TELEMETRY·ACK를 보낼 GW 주소")
    p.add_argument("--gw-port", type=int, default=GW_VEHICLE_PORT, help="GW 차량 수신 포트 (IF-01a)")
    p.add_argument("--rate", type=float, default=10.0, help="TELEMETRY Hz (기본 10)")
    p.add_argument("--count", type=int, default=None, help="보낼 TELEMETRY 개수 (기본: 무한)")
    p.add_argument("--drop-first", type=int, default=0, metavar="N",
                   help="시험용: 처음 받은 COMMAND N개를 무시한다 (ACK 없음)")
    p.add_argument("-v", "--verbose", action="store_true", help="보낸 TELEMETRY 프레임 hex 출력")
    a = p.parse_args(argv)
    sent = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((a.host, a.port))
        try:
            sent = run(sock, (a.gw_host, a.gw_port), a.rate, a.count, a.drop_first, a.verbose)
        except KeyboardInterrupt:
            pass
    print(f"sent {sent} TELEMETRY to {a.gw_host}:{a.gw_port}", file=sys.stderr)
