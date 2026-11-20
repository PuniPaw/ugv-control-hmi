"""SIM 수신부 해석: IF-01b(UDP 15010)로 GW가 보내는 COMMAND·HEARTBEAT 데이터그램을 검증·해석한다.

검증(길이, sync, version, CRC, 허용 type, type별 길이) 후 Heartbeat / ReceivedCommand / InvalidCommand로 돌려준다.
COMMAND 필드 범위는 ICD 표 6-5에 따라 프레임 폐기가 아니라 REJECTED_INVALID ACK 대상이다(cmd_id 0 제외).
소켓 입출력과 명령 처리는 `ugvc_sim.app`, `ugvc_sim.simulator`가 맡는다.
"""
from __future__ import annotations

from dataclasses import dataclass

from ugvc_proto.command import COMMAND_SIZE, CmdType, Command
from ugvc_proto.frame import MsgType, Reject, check_frame
from ugvc_proto.telemetry import LAT_LIMIT, LON_LIMIT

FAILSAFE_TIMEOUT_S = 3.0  # REQ-SAFE-001


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
