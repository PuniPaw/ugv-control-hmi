"""주기 송신의 예정 시각 (TELEMETRY 10Hz, ROUTE 1Hz). 시각은 호출자가 넣는다 (단조 시계 초)."""
from __future__ import annotations


class Schedule:
    """예정 시각을 처리할 때마다 한 주기(1 / rate_hz)씩 더한다.

    처리한 시각이 아니라 예정 시각에 더하므로 늦음이 쌓이지 않는다. 한 주기 넘게 늦었으면 밀린 몫을 몰아 보내지 않고
    지금부터 다시 잰다. 주기를 부동소수로 더해 가므로 10Hz 예정 시각은 정수 초와 정확히 겹치지 않는다(0.1을 30번 더하면
    3.0보다 조금 크다). 그래서 같은 시작 시각의 1Hz 예정 시각이 10Hz보다 아주 조금 먼저 온다.
    """

    def __init__(self, start: float, rate_hz: float):
        self.period = 1.0 / rate_hz
        self.due = start

    def advance(self, now: float) -> None:
        """이번 예정 시각을 처리했다. 다음 예정 시각으로 넘어가되, 그 시각도 이미 지났으면 now부터 다시 잰다."""
        self.due += self.period
        if self.due <= now:
            self.due = now + self.period
