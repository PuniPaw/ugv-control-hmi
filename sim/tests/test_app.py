"""SIM 프로세스 수준 시험: 실제 UDP 소켓·실제 시계로 app.run()을 돌리고 가짜 GW 소켓으로 TELEMETRY·ACK를 본다."""
import json
import socket
import threading
import time

import pytest

from ugvc_proto.command import Ack, AckResult, CmdType, Command
from ugvc_proto.frame import MsgType, check_frame, encode_frame
from ugvc_proto.telemetry import Flags, Mode, Telemetry

from ugvc_sim import app

PAYLOAD_LEN = {MsgType.TELEMETRY: 16, MsgType.ACK: 5}


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
            header, payload = check_frame(data, PAYLOAD_LEN)
            now = time.monotonic() - self.t0
            if header.msg_type == MsgType.TELEMETRY:
                self.telemetry.append((now, Telemetry.from_payload(payload)))
            else:
                self.acks.append(Ack.from_payload(payload))

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
    """HEARTBEAT가 한 번도 없으면 시작 3.0초 뒤 첫 TELEMETRY부터 STOPPED + FAILSAFE_ACTIVE (그 전은 MISSION, 플래그 없음)."""
    assert rig.wait_until(lambda: rig.now() > 3.6 and rig.telemetry, 5.0)
    first = next(t for t, tel in rig.telemetry if _failsafe(tel))
    assert 3.0 <= first <= 3.35, first
    assert all(tel.mode == Mode.MISSION and not tel.status_flags & Flags.FAILSAFE_ACTIVE
               for t, tel in rig.telemetry if t < first)
    assert all(_failsafe(tel) for t, tel in rig.telemetry if t >= first)
    frozen = {(tel.lat, tel.lon) for t, tel in rig.telemetry if t >= first}
    assert len(frozen) == 1  # 정지 후 위치가 변하지 않는다


def test_REQ_SAFE_002_process_resumes_only_on_command(rig):
    """Failsafe 뒤 HEARTBEAT가 돌아와도 정지·플래그 유지. COMMAND RTB를 받아야 ACCEPTED로 주행을 재개한다."""
    assert rig.wait_until(lambda: rig.telemetry and _failsafe(rig.last()), 5.0)
    stopped_at = rig.last()
    end = rig.now() + 1.5
    while rig.now() < end:  # HEARTBEAT 복귀
        rig.heartbeat()
        time.sleep(0.25)
    recovery = [tel for t, tel in rig.telemetry if t >= end - 1.5]
    assert recovery and all(_failsafe(tel) and (tel.lat, tel.lon) == (stopped_at.lat, stopped_at.lon) for tel in recovery)
    rig.command(Command(1, CmdType.RTB))
    assert rig.wait_until(lambda: rig.acks, 1.0)
    assert rig.acks[0] == Ack(1, AckResult.ACCEPTED)
    assert rig.wait_until(lambda: rig.last().mode == Mode.RTB, 1.0)
    assert not rig.last().status_flags & Flags.FAILSAFE_ACTIVE


class _UnreachableSocket:
    """GW 경로가 없는 소켓: sendto는 항상 OSError(ENETUNREACH), 수신은 아무것도 없음. 시계도 함께 진행한다."""

    def __init__(self):
        self.now = 0.0
        self.sends = 0

    def clock(self) -> float:
        return self.now

    def settimeout(self, timeout: float) -> None:
        self._timeout = timeout

    def recvfrom(self, _size: int):
        self.now += self._timeout  # 기다린 만큼 시간이 간다
        raise socket.timeout

    def sendto(self, _data: bytes, _addr) -> int:
        self.sends += 1
        raise OSError(101, "Network is unreachable")


def test_REQ_SAFE_001_send_failure_does_not_kill_sim_and_failsafe_still_engages(capsys):
    """GW로 가는 경로가 없어 송신이 모두 OSError여도 SIM은 죽지 않고, 시작 3.0초 뒤 Failsafe에 들어간다 (리뷰 #1)."""
    sock = _UnreachableSocket()
    sent = app.run(sock, ("192.0.2.1", 15001), count=40, clock=sock.clock)  # 4초 분량
    assert sent == 40 and sock.sends == 40
    lines = [json.loads(line) for line in capsys.readouterr().out.splitlines()]
    assert [l["type"] for l in lines if l["type"].startswith("send_")] == ["send_error"]  # 실패 시작에 한 줄만
    failsafe = [l for l in lines if l["type"] == "failsafe"]
    assert failsafe == [{"type": "failsafe", "active": True, "mode": "STOPPED"}]
