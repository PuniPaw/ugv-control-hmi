"""SIM 프로세스 수준 시험: 실제 UDP 소켓·실제 시계로 app.run()을 돌리고 가짜 GW 소켓으로 TELEMETRY·ACK·ROUTE를 본다."""
import json
import socket
import threading
import time

import pytest

from ugvc_proto.command import Ack, AckResult, CmdType, Command
from ugvc_proto.frame import MsgType, encode_frame
from ugvc_proto.route import Route
from ugvc_proto.telemetry import Flags, Mode, Telemetry

from downlink import parse_downlink
from ugvc_sim import app

NEAR_LAT, NEAR_LON = 361240000, 1277660000  # 시작 위치 근처 경로점 (≈ 60 m)


class Rig:
    """SIM 스레드 + 가짜 GW 소켓. GW가 받은 프레임을 (경과 초, 종류, 내용)으로 모은다."""

    def __init__(self):
        self.gw = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.gw.bind(("127.0.0.1", 0))
        self.gw.settimeout(0.2)
        self.sim_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sim_sock.bind(("127.0.0.1", 0))
        self.client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.stop = threading.Event()
        self.telemetry: list[tuple[float, Telemetry]] = []
        self.acks: list[Ack] = []
        self.routes: list[tuple[float, int, Route]] = []  # (경과 초, seq, 경로)
        self.t0 = time.monotonic()
        self._hb_seq = 0
        self.sim = threading.Thread(target=app.run, args=(self.sim_sock, self.gw.getsockname()),
                                    kwargs=dict(stop=self.stop), daemon=True)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.sim.start()
        self.reader.start()

    def _read(self):
        while not self.stop.is_set():
            try:
                data = self.gw.recvfrom(2048)[0]
            except socket.timeout:
                continue
            header, message = parse_downlink(data)
            now = time.monotonic() - self.t0
            if header.msg_type == MsgType.TELEMETRY:
                self.telemetry.append((now, message))
            elif header.msg_type == MsgType.ROUTE:
                self.routes.append((now, header.seq, message))
            else:
                self.acks.append(message)

    def now(self) -> float:
        return time.monotonic() - self.t0

    def heartbeat(self):
        self.client.sendto(encode_frame(MsgType.HEARTBEAT, self._hb_seq, 0), self.sim_sock.getsockname())
        self._hb_seq += 1

    def command(self, command: Command):
        self.client.sendto(encode_frame(MsgType.COMMAND, 0, 0, command.to_payload()), self.sim_sock.getsockname())

    def last(self) -> Telemetry:
        return self.telemetry[-1][1]

    def wait_until(self, pred, timeout_s: float) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if pred():
                return True
            time.sleep(0.02)
        return False

    def close(self):
        self.stop.set()
        self.sim.join(timeout=2)
        self.reader.join(timeout=2)
        for s in (self.gw, self.sim_sock, self.client):
            s.close()


@pytest.fixture
def rig():
    r = Rig()
    yield r
    r.close()


def _failsafe(t: Telemetry) -> bool:
    return t.mode == Mode.STOPPED and t.speed == 0 and bool(t.status_flags & Flags.FAILSAFE_ACTIVE)


def test_REQ_SAFE_001_process_without_heartbeat_reports_failsafe(rig):
    """HEARTBEAT가 한 번도 없으면 시작 3.0초 뒤 첫 TELEMETRY부터 STOPPED + FAILSAFE_ACTIVE.

    시작 직후 WAYPOINT로 주행시켜(시작 후 3.0초 전이라 HEARTBEAT 없이도 수락) 정지 전후가 구분되게 한다.
    그 전은 STANDBY(WAYPOINT 처리 전) 또는 주행 중인 MISSION이고 플래그 없음."""
    rig.command(Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON))
    assert rig.wait_until(lambda: rig.now() > 3.6 and rig.telemetry, 5.0)
    assert rig.acks == [Ack(1, AckResult.ACCEPTED)]
    first = next(t for t, tel in rig.telemetry if _failsafe(tel))
    assert 3.0 <= first <= 3.35, first
    before = [tel for t, tel in rig.telemetry if t < first]
    assert all(tel.mode in (Mode.STANDBY, Mode.MISSION) and not tel.status_flags & Flags.FAILSAFE_ACTIVE for tel in before)
    assert before[-1].mode == Mode.MISSION and before[-1].speed > 0
    assert (before[-1].lat, before[-1].lon) != (before[0].lat, before[0].lon)  # 정지 전에는 움직였다
    assert all(_failsafe(tel) for t, tel in rig.telemetry if t >= first)
    frozen = {(tel.lat, tel.lon) for t, tel in rig.telemetry if t >= first}
    assert len(frozen) == 1  # 정지 후 위치가 변하지 않는다


def test_REQ_SAFE_002_process_resumes_only_on_command(rig):
    """Failsafe 뒤 HEARTBEAT가 돌아와도 정지·플래그 유지. COMMAND WAYPOINT를 받아야 ACCEPTED로 주행을 재개한다.

    시작 위치에서 정지해 있으므로 RTB는 바로 도착해 STANDBY가 된다. 주행 재개는 WAYPOINT로 본다."""
    assert rig.wait_until(lambda: rig.telemetry and _failsafe(rig.last()), 5.0)
    stopped_at = rig.last()
    end = rig.now() + 1.5
    while rig.now() < end:  # HEARTBEAT 복귀
        rig.heartbeat()
        time.sleep(0.25)
    recovery = [tel for t, tel in rig.telemetry if t >= end - 1.5]
    assert recovery and all(_failsafe(tel) and (tel.lat, tel.lon) == (stopped_at.lat, stopped_at.lon) for tel in recovery)
    rig.heartbeat()
    rig.command(Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON))
    assert rig.wait_until(lambda: rig.acks, 1.0)
    assert rig.acks[0] == Ack(1, AckResult.ACCEPTED)
    assert rig.wait_until(lambda: rig.last().mode == Mode.MISSION and rig.last().speed > 0, 1.0)
    assert not rig.last().status_flags & Flags.FAILSAFE_ACTIVE


class _FakeSocket:
    """가짜 소켓 + 가짜 시계. 수신은 `inbox`의 (시각, 데이터그램)을 그 시각이 되면 내주고, 없으면 기다린 만큼 시간이 간다.

    `unreachable`이면 sendto는 항상 OSError(ENETUNREACH)다. 보내려던 프레임은 실패해도 `frames`에 남겨 내용을 판정한다.
    `stall`은 (시각, 초): 그 시각을 처음 지날 때 시계를 한 번 건너뛴다 (SIM 프로세스가 멈췄다 재개한 경우).
    """

    def __init__(self, unreachable=True, inbox=(), stall=None):
        self.now = 0.0
        self.unreachable = unreachable
        self.inbox = list(inbox)
        self.stall = stall
        self.frames: list[tuple[float, bytes]] = []

    def clock(self) -> float:
        if self.stall and self.now >= self.stall[0]:
            self.now += self.stall[1]
            self.stall = None
        return self.now

    def settimeout(self, timeout: float) -> None:
        self._timeout = timeout

    def recvfrom(self, _size: int):
        if self.inbox and self.inbox[0][0] <= self.now + self._timeout:
            at, data = self.inbox.pop(0)
            self.now = max(self.now, at)
            return data, ("127.0.0.1", 15001)
        self.now += self._timeout
        raise socket.timeout

    def sendto(self, data: bytes, _addr) -> int:
        self.frames.append((self.now, data))
        if self.unreachable:
            raise OSError(101, "Network is unreachable")
        return len(data)

    def sent(self, msg_type: MsgType) -> list[tuple[float, int, Telemetry | Ack | Route]]:
        """보내려던 프레임 중 msg_type인 것의 (시각, seq, 메시지)."""
        out = []
        for at, data in self.frames:
            header, message = parse_downlink(data)
            if header.msg_type == msg_type:
                out.append((at, header.seq, message))
        return out

    def telemetry(self) -> list[tuple[float, int, Telemetry]]:
        return self.sent(MsgType.TELEMETRY)

    def acks(self) -> list[Ack]:
        return [a for _, _, a in self.sent(MsgType.ACK)]


def test_REQ_SAFE_001_send_failure_does_not_kill_sim_and_failsafe_still_engages(capsys):
    """GW로 가는 경로가 없어 TELEMETRY·ACK 송신이 모두 OSError여도 SIM은 죽지 않고,
    시작 3.0초에 Failsafe에 들어간다: TELEMETRY seq 29(2.9s)는 플래그 없음, seq 30(3.0s)부터 FAILSAFE_ACTIVE (리뷰 1차 #1, 2차 #1)."""
    rtb = encode_frame(MsgType.COMMAND, 0, 0, Command(1, CmdType.STOP).to_payload())
    sock = _FakeSocket(inbox=[(1.05, rtb)])  # 1.05s에 COMMAND: ACK 송신도 실패하는 경로
    sent = app.run(sock, ("192.0.2.1", 15001), count=40, clock=sock.clock)  # 0.0~3.9s
    assert sent == 40
    tel = {seq: (at, t) for at, seq, t in sock.telemetry()}
    assert sorted(tel) == list(range(40))
    assert all(not t.status_flags & Flags.FAILSAFE_ACTIVE for seq, (_, t) in tel.items() if seq <= 29)
    # 1Hz ROUTE 예정 시각(정확히 3.0)이 TELEMETRY 예정 시각(0.1 누적 합, 3.0보다 조금 큼)보다 먼저 와서
    # 가짜 소켓의 최소 대기 1 ms만큼 늦을 수 있다
    assert 0 <= tel[30][0] - 3.0 <= 0.001 + 1e-9 and _failsafe(tel[30][1])
    assert all(_failsafe(t) for seq, (_, t) in tel.items() if seq >= 30)
    assert sock.acks() == [Ack(1, AckResult.ACCEPTED)]  # ACK도 보내려 했고 실패했지만 계속 돌았다
    lines = [json.loads(line) for line in capsys.readouterr().out.splitlines()]
    assert [l["type"] for l in lines if l["type"].startswith("send_")] == ["send_error"]  # 실패 시작에 한 줄만
    assert [l for l in lines if l["type"] == "failsafe"] == [{"type": "failsafe", "active": True, "mode": "STOPPED"}]


def test_REQ_SAFE_001_stalled_loop_sends_one_failsafe_telemetry_without_burst():
    """SIM 루프가 1.0s에 3.5초 멈췄다 재개하면 밀린 TELEMETRY를 몰아 보내지 않고 한 번 보내며, 그 TELEMETRY가 이미 Failsafe다."""
    sock = _FakeSocket(unreachable=False, stall=(1.0, 3.5))
    app.run(sock, ("127.0.0.1", 15001), count=20, clock=sock.clock)
    tel = sock.telemetry()
    times = [at for at, _, _ in tel]
    gaps = [b - a for a, b in zip(times, times[1:])]
    resumed = gaps.index(max(gaps)) + 1
    assert max(gaps) >= 3.5 and _failsafe(tel[resumed][2])  # 멈춘 뒤 첫 TELEMETRY가 이미 Failsafe
    assert all(not _failsafe(x) for _, _, x in tel[:resumed])
    assert all(g >= 0.1 - 1e-9 for g in gaps), gaps  # 밀린 주기를 같은 시각에 몰아 보내지 않는다


def test_REQ_CMD_007_process_sends_route_on_change_and_every_second():
    """SIM 본체(`app.run`, 가짜 소켓·가짜 시계): 시작에 빈 ROUTE, WAYPOINT를 받은 그 처리에서 ACK 뒤 바로 ROUTE,
    Failsafe 진입(경로 비움) tick에 바로 ROUTE, 그 사이는 시작 기준 1.0초 예정 시각마다. ROUTE는 자기 seq 0, 1, 2, … (ICD 6.6).
    가짜 소켓은 기다린 만큼 시계를 옮기고 최소 대기가 1 ms라 예정 시각보다 최대 1 ms 늦을 수 있다."""
    hb = encode_frame(MsgType.HEARTBEAT, 0, 0)
    wp = encode_frame(MsgType.COMMAND, 0, 0, Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON).to_payload())
    sock = _FakeSocket(unreachable=False, inbox=[(0.5, hb), (1.05, wp)])  # 마지막 HEARTBEAT 0.5s → Failsafe 3.5s
    app.run(sock, ("127.0.0.1", 15001), count=50, clock=sock.clock)  # 0.0~4.9s
    routes = sock.sent(MsgType.ROUTE)
    assert [seq for _, seq, _ in routes] == list(range(len(routes)))
    assert routes[0][0] == 0.0 and routes[0][2] == Route()
    types = [parse_downlink(data)[0].msg_type for _, data in sock.frames]
    ack_at = types.index(MsgType.ACK)
    assert types[ack_at + 1] == MsgType.ROUTE  # 같은 처리 주기 안, ACK 바로 뒤
    at, _, route = next(r for r in routes if r[2].waypoints)
    assert at == 1.05 and route == Route(((NEAR_LAT, NEAR_LON),))
    failsafe_at = next(t for t, _, tel in sock.telemetry() if _failsafe(tel))
    assert abs(failsafe_at - 3.5) < 0.1 + 1e-9
    after = [(t, r) for t, _, r in routes if t >= failsafe_at]
    assert after[0] == (failsafe_at, Route())  # Failsafe tick에 바로 빈 경로
    periodic = [t for t, _, _ in routes if t not in (1.05, failsafe_at)]
    assert len(periodic) == 5 and all(0 <= t - k <= 0.001 + 1e-9 for k, t in enumerate(periodic)), periodic
    assert all(r.waypoints for t, _, r in routes if 1.05 <= t < failsafe_at)


def test_REQ_SAFE_003_process_starts_standby_and_stays_without_command():
    """SIM 본체(`app.run`, 가짜 시계): HEARTBEAT가 계속 와도 명령이 없으면 TELEMETRY가 내내 STANDBY·speed 0·같은 위치."""
    inbox = [(i * 0.5, encode_frame(MsgType.HEARTBEAT, i, 0)) for i in range(1, 20)]
    sock = _FakeSocket(unreachable=False, inbox=inbox)
    app.run(sock, ("127.0.0.1", 15001), count=100, clock=sock.clock)  # 0.0~9.9s
    tel = [t for _, _, t in sock.telemetry()]
    assert len(tel) == 100
    assert all(t.mode == Mode.STANDBY and t.speed == 0 and not t.status_flags & Flags.FAILSAFE_ACTIVE for t in tel)
    assert len({(t.lat, t.lon) for t in tel}) == 1


@pytest.mark.parametrize("rate_hz", [10.0, 0.4])
def test_REQ_CMD_007_process_sends_no_stale_route_once_failsafe_holds(rate_hz):
    """마지막 HEARTBEAT(1.0s) 뒤 3.0s인 4.0s가 1Hz ROUTE 예정 시각과 겹쳐도, 그 ROUTE부터 경로는 비어 있다.
    ROUTE 송신 앞에서도 Failsafe를 먼저 판정한다. TELEMETRY가 드문 0.4Hz에서도 같다."""
    hb0, hb1 = encode_frame(MsgType.HEARTBEAT, 0, 0), encode_frame(MsgType.HEARTBEAT, 1, 0)
    wp = encode_frame(MsgType.COMMAND, 0, 0, Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON).to_payload())
    sock = _FakeSocket(unreachable=False, inbox=[(0.5, hb0), (0.6, wp), (1.0, hb1)])
    app.run(sock, ("127.0.0.1", 15001), rate_hz=rate_hz, count=int(5.0 * rate_hz) + 1, clock=sock.clock)  # 0 ~ 5.0s
    routes = sock.sent(MsgType.ROUTE)
    assert any(r.waypoints for t, _, r in routes if t < 4.0)  # 두절 전에는 경로가 있었다
    late = [(t, len(r.waypoints)) for t, _, r in routes if t >= 4.0]
    assert late and all(n == 0 for _, n in late), late


@pytest.mark.parametrize("rate_hz", [10.0, 1.0, 0.4])
def test_REQ_CMD_007_process_reports_empty_route_within_one_wait_after_failsafe(rate_hz):
    """Failsafe 기한(마지막 HEARTBEAT 1.1s + 3.0s = 4.1s)이 TELEMETRY·ROUTE 예정 시각과 겹치지 않아도, 빈 ROUTE는
    기한 뒤 소켓 대기 한 번(최대 0.1s) 안에 나간다. 수신 시간 초과 뒤에는 기다린 뒤의 시각으로 판정한다."""
    hb0, hb1 = encode_frame(MsgType.HEARTBEAT, 0, 0), encode_frame(MsgType.HEARTBEAT, 1, 0)
    wp = encode_frame(MsgType.COMMAND, 0, 0, Command(1, CmdType.WAYPOINT, NEAR_LAT, NEAR_LON).to_payload())
    sock = _FakeSocket(unreachable=False, inbox=[(0.5, hb0), (0.6, wp), (1.1, hb1)])
    app.run(sock, ("127.0.0.1", 15001), rate_hz=rate_hz, count=int(5.0 * rate_hz) + 1, clock=sock.clock)
    deadline = 1.1 + 3.0
    first_empty = next(t for t, _, r in sock.sent(MsgType.ROUTE) if t >= deadline and not r.waypoints)
    assert first_empty - deadline <= 0.1 + 1e-9, first_empty
