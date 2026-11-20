"""SIM 핵심 로직: 차량 상태 + HEARTBEAT 감시(Failsafe) + 명령 처리. 소켓·시계가 없는 순수 로직이다.

시각(`now`, 단조 시계 초)은 호출자가 넣는다. 모든 사건(명령, HEARTBEAT, 주기 tick)의 맨 앞에서
Failsafe 조건을 먼저 평가하므로, 사건이 어떤 순서로 도착해도 3.0초 두절 뒤에는 정지 상태가 먼저 반영된다.
"""
from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass

from ugvc_proto.command import Ack, AckResult, CmdType, Command
from ugvc_proto.telemetry import Flags, Telemetry

from ugvc_sim.uplink import HeartbeatWatch, InvalidCommand, ReceivedCommand
from ugvc_sim.vehicle import HOME, START, LatLon, VehicleState, apply_command, enter_failsafe, step, to_telemetry

ACK_MEMORY = 1024  # REQ-CMD-003: 중복 판정을 위해 기억하는 최근 cmd_id 수


@dataclass(frozen=True)
class CommandOutcome:
    ack: Ack
    execute: Command | None  # 이번에 차량 상태에 적용한 명령. 중복·거절, 두절 중 STOP(이미 정지)이면 None


class Simulator:
    def __init__(self, start: float = 0.0, state: VehicleState = START, home: LatLon = HOME,
                 memory: int = ACK_MEMORY):
        self.state = state
        self.home = home
        self._watch = HeartbeatWatch(start)
        self._memory = memory
        self._results: OrderedDict[int, AckResult] = OrderedDict()

    def _check_failsafe(self, now: float) -> None:
        """REQ-SAFE-001: HEARTBEAT(없으면 시작) 후 3.0초 이상이면 정지. 이미 Failsafe면 아무 일 없다."""
        if self._watch.expired(now):
            self.state = enter_failsafe(self.state)

    def on_heartbeat(self, now: float) -> None:
        """HEARTBEAT 수신. 그 사이 3.0초가 지났다면 먼저 Failsafe로 넣는다. 복귀는 Failsafe를 풀지 않는다 (REQ-SAFE-002)."""
        self._check_failsafe(now)
        self._watch.on_heartbeat(now)

    def on_command(self, msg: ReceivedCommand | InvalidCommand, now: float) -> CommandOutcome:
        self._check_failsafe(now)
        cmd = msg.command
        previous = self._results.get(cmd.cmd_id)
        if previous is not None:
            return CommandOutcome(Ack(cmd.cmd_id, previous), None)  # 같은 cmd_id: 첫 결과 재응답, 재실행 없음
        outcome = self._decide(msg, now)
        self._results[cmd.cmd_id] = AckResult(outcome.ack.result)
        if len(self._results) > self._memory:
            self._results.popitem(last=False)
        return outcome

    def _decide(self, msg: ReceivedCommand | InvalidCommand, now: float) -> CommandOutcome:
        cmd = msg.command
        if isinstance(msg, InvalidCommand):
            return CommandOutcome(Ack(cmd.cmd_id, AckResult.REJECTED_INVALID), None)
        if self._watch.expired(now):
            # 링크 두절 중: STOP은 이미 정지 상태라 수락만 하고 Failsafe는 유지. 이동 명령은 거절 (REQ-SAFE-002)
            if cmd.cmd_type == CmdType.STOP:
                return CommandOutcome(Ack(cmd.cmd_id, AckResult.ACCEPTED), None)
            return CommandOutcome(Ack(cmd.cmd_id, AckResult.REJECTED_STATE), None)
        self.state = apply_command(self.state, cmd)
        return CommandOutcome(Ack(cmd.cmd_id, AckResult.ACCEPTED), cmd)

    def advance(self, now: float, dt: float) -> None:
        self._check_failsafe(now)
        self.state = step(self.state, dt, self.home)

    def telemetry(self) -> Telemetry:
        return to_telemetry(self.state)

    @property
    def failsafe(self) -> bool:
        return bool(self.state.flags & Flags.FAILSAFE_ACTIVE)
