"""SIM 수신부: IF-01b(UDP 15010)로 GW가 보내는 COMMAND·HEARTBEAT를 받는다.

현재 단계: 수신 → 검증(길이, sync, version, CRC, 허용 type, type별 길이, COMMAND 필드 범위) → 해석 → 출력.
명령 실행·ACK 응답(REQ-CMD-003)과 HEARTBEAT 3.0s 두절 Failsafe(REQ-SAFE-001)는 12월에 이 위에 붙인다.

사용:
    python3 -m ugvc_sim.uplink            # UDP 15010에서 받은 메시지를 JSON 한 줄씩 출력
"""
from __future__ import annotations

import argparse
import json
import socket
import time
from dataclasses import asdict, dataclass

from ugvc_proto.command import COMMAND_SIZE, MSG_COMMAND, MSG_HEARTBEAT, CmdType, Command
from ugvc_proto.frame import (FRAME_OVERHEAD, HEADER_SIZE, MAX_FRAME_SIZE, SYNC, VERSION, crc16_ccitt_false,
                              decode_header, read_frame_crc)

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


Uplink = Heartbeat | ReceivedCommand

# msg_type별 payload 길이. 여기 없는 type은 SIM이 받지 않는다.
_PAYLOAD_LEN = {MSG_HEARTBEAT: 0, MSG_COMMAND: COMMAND_SIZE}

LAT_LIMIT = 900_000_000    # 1e-7 deg, ICD 6.1 (COMMAND 좌표는 TELEMETRY와 같은 범위)
LON_LIMIT = 1_800_000_000


_CMD_TYPES = {t.value for t in CmdType}


def _command_in_range(c: Command) -> bool:
    """ICD 6.3: cmd_id ≥ 1, cmd_type은 표 6-4, WAYPOINT 좌표는 TELEMETRY 범위. 그 외 명령의 좌표는 쓰지 않는다."""
    if c.cmd_id < 1 or c.cmd_type not in _CMD_TYPES:
        return False
    if c.cmd_type == CmdType.WAYPOINT:
        return -LAT_LIMIT <= c.lat <= LAT_LIMIT and -LON_LIMIT <= c.lon <= LON_LIMIT
    return True


def parse_uplink(data: bytes) -> Uplink | str:
    """해석한 메시지, 또는 폐기 원인(GW 오류 계수와 같은 이름: length, sync, version, crc, type, range)."""
    if len(data) < FRAME_OVERHEAD or len(data) > MAX_FRAME_SIZE:
        return "length"
    h = decode_header(data)
    if h.sync != SYNC:
        return "sync"
    if h.version != VERSION:
        return "version"
    if len(data) != FRAME_OVERHEAD + h.payload_len:
        return "length"
    if read_frame_crc(data) != crc16_ccitt_false(data[:-2]):
        return "crc"
    if h.msg_type not in _PAYLOAD_LEN:
        return "type"
    if h.payload_len != _PAYLOAD_LEN[h.msg_type]:
        return "length"
    if h.msg_type == MSG_HEARTBEAT:
        return Heartbeat(h.seq, h.timestamp_ms)
    command = Command.from_payload(data[HEADER_SIZE:-2])
    if not _command_in_range(command):
        return "range"
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


def listen(host: str, port: int) -> None:
    watch = HeartbeatWatch(start=time.monotonic())
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((host, port))
        while True:
            data, _ = sock.recvfrom(2048)
            now = time.monotonic()
            msg = parse_uplink(data)
            if isinstance(msg, str):
                print(json.dumps({"type": "reject", "reason": msg, "len": len(data)}), flush=True)
                continue
            if isinstance(msg, Heartbeat):
                watch.on_heartbeat(now)
            kind = "heartbeat" if isinstance(msg, Heartbeat) else "command"
            print(json.dumps({"type": kind, **asdict(msg)}), flush=True)


def main(argv: list[str] | None = None) -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=15010)
    a = p.parse_args(argv)
    try:
        listen(a.host, a.port)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
