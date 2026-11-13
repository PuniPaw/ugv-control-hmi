"""SIM 수신부: IF-01b(UDP 15010)로 GW가 보내는 COMMAND·HEARTBEAT를 받는다.

현재 단계: 수신 → 검증(길이, sync, version, CRC, 허용 type, type별 길이) → 해석 → COMMAND ACK 응답 → 출력.
COMMAND 필드 범위는 ICD 표 6-5에 따라 프레임 폐기가 아니라 REJECTED_INVALID ACK로 답한다(cmd_id 0 제외).
차량 거동 변경(STOP/RTB/WAYPOINT 적용)과 HEARTBEAT 3.0s 두절 Failsafe(REQ-SAFE-001)는 이 위에 붙인다.

사용:
    python3 -m ugvc_sim.uplink            # UDP 15010에서 받은 메시지를 JSON 한 줄씩 출력, ACK는 GW 15001로
"""
from __future__ import annotations

import argparse
import json
import socket
import threading
import time
from collections import OrderedDict
from dataclasses import asdict, dataclass

from ugvc_proto.command import COMMAND_SIZE, Ack, AckResult, CmdType, Command
from ugvc_proto.frame import MsgType, Reject, check_frame, encode_frame
from ugvc_proto.telemetry import LAT_LIMIT, LON_LIMIT

FAILSAFE_TIMEOUT_S = 3.0  # REQ-SAFE-001
ACK_MEMORY = 1024  # REQ-CMD-003: 중복 판정을 위해 기억하는 최근 cmd_id 수
GW_VEHICLE_PORT = 15001  # IF-01a. GW는 ACK를 이 포트로 받는다


@dataclass(frozen=True)
class Heartbeat:
    seq: int
    timestamp_ms: int


@dataclass(frozen=True)
class ReceivedCommand:
    seq: int
    timestamp_ms: int
    command: Command


@dataclass(frozen=True)
class InvalidCommand:
    """프레임은 정상이나 필드가 범위 밖인 COMMAND (cmd_id ≥ 1). 실행하지 않고 REJECTED_INVALID로 ACK한다."""
    seq: int
    timestamp_ms: int
    command: Command


Uplink = Heartbeat | ReceivedCommand | InvalidCommand

# SIM이 받는 msg_type과 payload 길이 (IF-01b). 여기 없는 type은 ⑥ type으로 폐기.
_UPLINK_PAYLOAD_LEN = {MsgType.HEARTBEAT: 0, MsgType.COMMAND: COMMAND_SIZE}

_CMD_TYPES = {t.value for t in CmdType}


def _fields_in_range(c: Command) -> bool:
    """ICD 6.3: cmd_type은 표 6-4, WAYPOINT 좌표는 TELEMETRY 범위. 그 외 명령의 좌표는 쓰지 않는다."""
    if c.cmd_type not in _CMD_TYPES:
        return False
    if c.cmd_type == CmdType.WAYPOINT:
        return -LAT_LIMIT <= c.lat <= LAT_LIMIT and -LON_LIMIT <= c.lon <= LON_LIMIT
    return True


def parse_uplink(data: bytes) -> Uplink | Reject:
    """해석한 메시지, 또는 폐기 원인 (규칙 ①~⑦은 ugvc_proto.frame.check_frame, ⑧ COMMAND 범위는 여기서).

    ⑧: cmd_id 0은 ACK할 대상이 없어 폐기(Reject.RANGE). cmd_id ≥ 1인 범위 밖 필드는 InvalidCommand (ICD 표 6-5).
    """
    checked = check_frame(data, _UPLINK_PAYLOAD_LEN)
    if isinstance(checked, Reject):
        return checked
    h, payload = checked
    if h.msg_type == MsgType.HEARTBEAT:
        return Heartbeat(h.seq, h.timestamp_ms)
    command = Command.from_payload(payload)
    if command.cmd_id < 1:
        return Reject.RANGE
    if not _fields_in_range(command):
        return InvalidCommand(h.seq, h.timestamp_ms, command)
    return ReceivedCommand(h.seq, h.timestamp_ms, command)


@dataclass(frozen=True)
class CommandOutcome:
    ack: Ack
    execute: Command | None  # 이번에 실제로 실행할 명령. 중복이거나 거절이면 None


class CommandResponder:
    """cmd_id별 첫 결과를 기억해 같은 cmd_id가 다시 오면 같은 ACK만 재전송한다 (REQ-CMD-003).

    최근 `memory`개 cmd_id만 기억하고 가장 오래된 것부터 잊는다. 순수 로직이라 소켓·시계가 없다.
    """

    def __init__(self, memory: int = ACK_MEMORY):
        self._memory = memory
        self._results: OrderedDict[int, AckResult] = OrderedDict()

    def on_command(self, msg: ReceivedCommand | InvalidCommand) -> CommandOutcome:
        cmd = msg.command
        previous = self._results.get(cmd.cmd_id)
        if previous is not None:
            return CommandOutcome(Ack(cmd.cmd_id, previous), None)
        accepted = isinstance(msg, ReceivedCommand)
        result = AckResult.ACCEPTED if accepted else AckResult.REJECTED_INVALID
        self._results[cmd.cmd_id] = result
        if len(self._results) > self._memory:
            self._results.popitem(last=False)
        return CommandOutcome(Ack(cmd.cmd_id, result), cmd if accepted else None)


def ack_for_command(frame: bytes) -> Ack | None:
    """한 COMMAND 프레임에 대한 첫 수신 ACK. COMMAND가 아니거나 폐기 대상(cmd_id 0 포함)이면 None. 상태 없음."""
    msg = parse_uplink(frame)
    if not isinstance(msg, (ReceivedCommand, InvalidCommand)):
        return None
    return CommandResponder().on_command(msg).ack


class HeartbeatWatch:
    """마지막 HEARTBEAT 이후 경과 시간 (REQ-SAFE-001). 시각은 호출자가 넣는다 (time.monotonic 등).

    시작 시각부터 잰다: 한 번도 못 받은 경우도 "3.0초 이상 미수신"이다. GW가 꺼져 있거나 HEARTBEAT가
    엉뚱한 주소로 가는데도 차량이 계속 달리는 일이 없도록 하는 안전 쪽 기본값이다.
    """

    def __init__(self, start: float, timeout_s: float = FAILSAFE_TIMEOUT_S):
        self.timeout_s = timeout_s
        self._last = start

    def on_heartbeat(self, now: float) -> None:
        self._last = now

    def age(self, now: float) -> float:
        return now - self._last

    def expired(self, now: float) -> bool:
        """마지막 HEARTBEAT(없으면 시작) 후 3.0초 이상 지났는가."""
        return self.age(now) >= self.timeout_s


def serve(sock: socket.socket, gw_addr: tuple[str, int], drop_first: int = 0,
          stop: threading.Event | None = None) -> None:
    """바인드된 소켓에서 받아 처리한다. `stop`이 켜지면 끝난다 (시험용. 운영은 Ctrl-C).

    drop_first: 처음 받은 유효 COMMAND 데이터그램 N개를 무시한다 (ACK 없음, 실행 없음).
    GW 재전송 시험을 위한 장치이며 운영 기본값은 0이다.
    """
    sock.settimeout(0.1)
    watch = HeartbeatWatch(start=time.monotonic())
    responder = CommandResponder()
    ack_seq = 0
    to_drop = drop_first
    while stop is None or not stop.is_set():
        try:
            data, _ = sock.recvfrom(2048)
        except socket.timeout:
            continue
        now = time.monotonic()
        msg = parse_uplink(data)
        if isinstance(msg, Reject):
            print(json.dumps({"type": "reject", "reason": msg.value, "len": len(data)}), flush=True)
        elif isinstance(msg, Heartbeat):
            watch.on_heartbeat(now)
            print(json.dumps({"type": "heartbeat", **asdict(msg)}), flush=True)
        elif to_drop > 0:
            to_drop -= 1
            print(json.dumps({"type": "drop", "cmd_id": msg.command.cmd_id}), flush=True)
        else:
            outcome = responder.on_command(msg)
            ack_frame = encode_frame(MsgType.ACK, ack_seq, int(time.time() * 1000), outcome.ack.to_payload())
            ack_seq = (ack_seq + 1) & 0xFFFFFFFF
            sock.sendto(ack_frame, gw_addr)
            print(json.dumps({
                "type": "command", "cmd_id": msg.command.cmd_id, "cmd_type": msg.command.cmd_type,
                "result": AckResult(outcome.ack.result).name, "executed": outcome.execute is not None,
            }), flush=True)


def listen(host: str, port: int, gw_addr: tuple[str, int], drop_first: int = 0) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((host, port))
        serve(sock, gw_addr, drop_first)


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=15010)
    p.add_argument("--gw-host", default="127.0.0.1", help="ACK를 보낼 GW 주소")
    p.add_argument("--gw-port", type=int, default=GW_VEHICLE_PORT, help="GW 차량 수신 포트 (IF-01a)")
    p.add_argument("--drop-first", type=int, default=0, metavar="N",
                   help="시험용: 처음 받은 COMMAND N개를 무시한다 (ACK 없음)")
    a = p.parse_args(argv)
    try:
        listen(a.host, a.port, (a.gw_host, a.gw_port), a.drop_first)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
