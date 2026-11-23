"""SIM 실행 본체: UDP 소켓 하나에서 COMMAND·HEARTBEAT를 받고 10Hz로 TELEMETRY를 보낸다 (단일 스레드).

사용:
    python3 -m ugvc_sim                    # UDP 15010 수신, TELEMETRY·ACK는 GW 127.0.0.1:15001로, 10Hz 무한
    python3 -m ugvc_sim --count 600 -v     # TELEMETRY 600개 뒤 종료, 보낸 프레임 hex 출력

표준출력은 JSON 한 줄씩: command / heartbeat / reject / drop, 모드·Failsafe 전이(mode / failsafe),
송수신 오류(send_error / send_ok / recv_error). 송신이 실패해도 SIM은 멈추지 않는다.
"""
from __future__ import annotations

import argparse
import errno
import ipaddress
import json
import math
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
_MAX_RATE_HZ = 1000.0
_FATAL_SOCKET_ERRNOS = {errno.EBADF, errno.ENOTSOCK}  # 소켓 자체가 없다: 계속 돌아도 회복할 수 없음


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


class _Sender:
    """GW로 보내는 송신. 경로가 없는 등 OSError가 나도 기록만 하고 넘긴다 (리뷰 #1).

    송신이 실패해도 수신·Failsafe 판정은 계속 돌아야 한다. 두절 상황에서 SIM이 죽으면 Failsafe가 사라진다.
    기록은 실패가 시작될 때와 회복될 때만 한 줄씩 쓴다 (10Hz로 줄이 쏟아지지 않게).
    """

    def __init__(self, sock: socket.socket, gw_addr: tuple[str, int]):
        self._sock, self._addr, self._failing = sock, gw_addr, False

    def send(self, frame: bytes) -> None:
        try:
            self._sock.sendto(frame, self._addr)
        except OSError as e:
            if not self._failing:
                self._failing = True
                _emit({"type": "send_error", "errno": e.errno, "error": str(e)})
            return
        if self._failing:
            self._failing = False
            _emit({"type": "send_ok"})


def run(sock: socket.socket, gw_addr: tuple[str, int], rate_hz: float = 10.0, count: int | None = None,
        drop_first: int = 0, verbose: bool = False, stop: threading.Event | None = None,
        clock: Callable[[], float] = time.monotonic) -> int:
    """바인드된 소켓으로 SIM을 돌린다. 보낸 TELEMETRY 개수를 돌려준다.

    `stop`이 켜지거나 `count`개를 보내거나 Ctrl-C면 끝난다. gw_addr는 해석된 (IP, 포트)다.
    drop_first: 처음 받은 유효 COMMAND N개를 무시한다 (ACK·실행 없음). GW 재전송 시험용이며 기본 0.
    """
    period = 1.0 / rate_hz
    sender = _Sender(sock, gw_addr)
    t0 = clock()
    sim = Simulator(start=t0)
    tel_seq = ack_seq = sent = 0
    due = last_tick = t0
    to_drop = drop_first
    recv_failing = False
    try:
        while (stop is None or not stop.is_set()) and (count is None or sent < count):
            now = clock()
            if now >= due:
                before = (int(sim.state.mode), sim.failsafe)
                sim.advance(now, now - last_tick)  # 실제 경과 시간만큼 (첫 TELEMETRY는 시작 상태 그대로)
                last_tick = now
                _report_transitions(sim, before)
                frame = encode_frame(MsgType.TELEMETRY, tel_seq, _epoch_ms(), sim.telemetry().to_payload())
                sender.send(frame)
                if verbose:
                    print(f"seq={tel_seq} {frame.hex()}", flush=True)
                tel_seq = (tel_seq + 1) & 0xFFFFFFFF
                sent += 1
                # 예정 시각 기준이라 늦음이 쌓이지 않는다. 한 주기 넘게 늦었으면 몰아 보내지 않고 지금부터 다시 잰다
                due += period
                if due <= now:
                    due = now + period
                continue
            sock.settimeout(max(0.001, min(due - now, _MAX_WAIT_S)))
            try:
                data, _ = sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError as e:  # 일시 오류(예: ICMP 오류)는 계속. 소켓 자체가 없으면 다시 던진다
                if e.errno in _FATAL_SOCKET_ERRNOS:
                    raise
                if not recv_failing:
                    recv_failing = True
                    _emit({"type": "recv_error", "errno": e.errno, "error": str(e)})
                continue
            recv_failing = False
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
                # 받기 전 구간은 이전 명령대로 진행해 두고 새 명령을 적용한다 (주기 중간 명령이 주기 시작부터 적용되지 않게)
                sim.advance(now, now - last_tick)
                last_tick = now
                outcome = sim.on_command(msg, now)
                sender.send(encode_frame(MsgType.ACK, ack_seq, _epoch_ms(), outcome.ack.to_payload()))
                ack_seq = (ack_seq + 1) & 0xFFFFFFFF
                _emit({"type": "command", "cmd_id": msg.command.cmd_id, "cmd_type": msg.command.cmd_type,
                       "result": AckResult(outcome.ack.result).name, "executed": outcome.execute is not None})
            _report_transitions(sim, before)
    except KeyboardInterrupt:
        pass
    return sent


def _rate(text: str) -> float:
    v = float(text)
    if not (math.isfinite(v) and 0 < v <= _MAX_RATE_HZ):
        raise argparse.ArgumentTypeError(f"0보다 크고 {_MAX_RATE_HZ:g} 이하여야 한다")
    return v


def _resolve(host: str, port: int) -> tuple[str, int]:
    """GW 주소를 시작 때 한 번 IPv4로 해석한다. 실행 중 이름 해석 실패로 송신이 죽지 않게 한다."""
    return socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_DGRAM)[0][4][:2]


def _is_loopback(host: str) -> bool:
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return host == "localhost"


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(prog="python3 -m ugvc_sim", description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host", default="127.0.0.1", help="COMMAND·HEARTBEAT를 받을 주소")
    p.add_argument("--port", type=int, default=15010, help="수신 포트 (IF-01b)")
    p.add_argument("--gw-host", default="127.0.0.1", help="TELEMETRY·ACK를 보낼 GW 주소")
    p.add_argument("--gw-port", type=int, default=GW_VEHICLE_PORT, help="GW 차량 수신 포트 (IF-01a)")
    p.add_argument("--rate", type=_rate, default=10.0, help="TELEMETRY Hz (기본 10)")
    p.add_argument("--count", type=int, default=None, help="보낼 TELEMETRY 개수 (기본: 무한)")
    p.add_argument("--drop-first", type=int, default=0, metavar="N",
                   help="시험용: 처음 받은 COMMAND N개를 무시한다 (ACK 없음)")
    p.add_argument("-v", "--verbose", action="store_true", help="보낸 TELEMETRY 프레임 hex 출력")
    a = p.parse_args(argv)
    try:
        gw_addr = _resolve(a.gw_host, a.gw_port)
    except (OSError, UnicodeError) as e:
        p.error(f"GW 주소 {a.gw_host}:{a.gw_port}를 해석할 수 없다: {e}")
    if _is_loopback(a.host) and not ipaddress.ip_address(gw_addr[0]).is_loopback:
        p.error(f"--host {a.host}(루프백)에 바인드하면 루프백이 아닌 GW {gw_addr[0]}로 보낼 수 없다. --host 0.0.0.0 등을 준다")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((a.host, a.port))
        sent = run(sock, gw_addr, a.rate, a.count, a.drop_first, a.verbose)
    print(f"sent {sent} TELEMETRY to {a.gw_host}:{a.gw_port}", file=sys.stderr)
