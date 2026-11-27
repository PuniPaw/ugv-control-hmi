"""SIM 핵심 로직: 차량 상태 + HEARTBEAT 감시(Failsafe) + 명령 처리 + ROUTE 송신 시점. 소켓·시계가 없는 순수 로직이다.

시각(`now`, 단조 시계 초)은 호출자가 넣는다. 모든 사건(명령, HEARTBEAT, 주기 tick)의 맨 앞에서
Failsafe 조건을 먼저 평가하므로, 사건이 어떤 순서로 도착해도 3.0초 두절 뒤에는 정지 상태가 먼저 반영된다.
"""
from __future__ import annotations

from collections import OrderedDict
from dataclasses import dataclass

from ugvc_proto.command import Ack, AckResult, CmdType, Command
from ugvc_proto.route import MAX_WAYPOINTS, Route
from ugvc_proto.telemetry import Flags, Telemetry

from ugvc_sim.schedule import Schedule
from ugvc_sim.uplink import HeartbeatWatch, InvalidCommand, ReceivedCommand
from ugvc_sim.vehicle import (HOME, START, LatLon, VehicleState, apply_command, enter_failsafe, step, to_route,
                              to_telemetry)

ACK_MEMORY = 1024  # REQ-CMD-003: 중복 판정을 위해 기억하는 최근 cmd_id 수
ROUTE_PERIOD_S = 1.0  # REQ-CMD-007: 바뀌지 않아도 ROUTE를 보내는 주기


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
        if cmd.cmd_type == CmdType.WAYPOINT and len(self.state.route) >= MAX_WAYPOINTS:
            return CommandOutcome(Ack(cmd.cmd_id, AckResult.REJECTED_STATE), None)  # 경로가 가득 참 (REQ-CMD-007)
        self.state = apply_command(self.state, cmd)
        return CommandOutcome(Ack(cmd.cmd_id, AckResult.ACCEPTED), cmd)

    def advance(self, now: float, dt: float) -> None:
        self._check_failsafe(now)
        self.state = step(self.state, dt, self.home)

    def telemetry(self) -> Telemetry:
        return to_telemetry(self.state)

    def route(self, now: float) -> Route:
        """지금 남은 경로. 송신하는 값이므로 다른 사건처럼 Failsafe 조건부터 평가한다 (두절 3.0초면 비어 있음)."""
        self._check_failsafe(now)
        return to_route(self.state)

    @property
    def failsafe(self) -> bool:
        return bool(self.state.flags & Flags.FAILSAFE_ACTIVE)


class RouteReporter:
    """ROUTE를 보낼 때인지 정한다 (ICD 6.6, REQ-CMD-007). 시각(단조 시계 초)은 호출자가 넣는다.

    남은 경로가 마지막으로 보낸 것과 다르면 바로, 같아도 1초 주기 예정 시각이 되면 보낸다.
    호출자는 경로를 바꿀 수 있는 사건(명령, HEARTBEAT, 주기 tick)을 처리할 때마다, 그리고 `due`가 되면 poll한다.
    주기는 `Schedule`이라 늦음이 쌓이지 않고, 한 주기 넘게 늦었으면 몰아 보내지 않고 지금부터 다시 잰다.
    """

    def __init__(self, start: float, period_s: float = ROUTE_PERIOD_S):
        self._schedule = Schedule(start, 1.0 / period_s)  # 첫 예정 시각이 start: 시작하자마자 빈 경로도 보고
        self._sent: Route | None = None

    @property
    def due(self) -> float:
        """다음 주기 송신 예정 시각. 호출자는 이 시각에도 poll한다."""
        return self._schedule.due

    def poll(self, now: float, route: Route) -> bool:
        """이번에 route를 보내야 하면 True (보낸 것으로 기록한다)."""
        periodic = now >= self._schedule.due
        if not periodic and route == self._sent:
            return False
        self._sent = route
        if periodic:
            self._schedule.advance(now)
        return True
