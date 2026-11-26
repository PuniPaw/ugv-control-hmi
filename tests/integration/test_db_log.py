"""DB 로그 저장 (REQ-LOG-001·003, SDD 5.1).

실제 GW를 `--db`로 띄우고 임시 PostgreSQL 클러스터(pg_cluster.py)에 저장된 내용을 psql로 확인한다.
DB를 내리거나 멈춰도 중계·HEARTBEAT가 계속되고, 버린 기록 수가 GW 진단 기록(stderr)에 남는지 본다.
PostgreSQL이 없는 환경에서는 PostgreSQL이 필요한 시험만 사유와 함께 skip한다.
"""
import asyncio
import json
import re
import socket
import struct
import threading
import time

import pytest
import websockets

from conftest import GATEWAY_BIN, free_udp_port, start_gateway, stop_gateway
from pg_cluster import PgCluster, unavailable_reason
from ugvc_proto.frame import MsgType, encode_frame
from ugvc_proto.telemetry import Telemetry


@pytest.fixture(scope="module")
def pg():
    if reason := unavailable_reason():
        pytest.skip(f"PostgreSQL 통합 시험 skip: {reason}")
    cluster = PgCluster()
    try:
        cluster.init()
        cluster.start()
        yield cluster
    finally:
        cluster.destroy()


@pytest.fixture
def start_gw():
    """추가 옵션으로 GW를 띄우는 함수. 시험이 끝나면 멈춘다."""
    if not GATEWAY_BIN.exists():
        pytest.skip(f"gateway not built: {GATEWAY_BIN}")
    started = []

    def start(*extra_args: str):
        started.append(start_gateway([free_udp_port() for _ in range(3)], extra_args))
        return started[-1]

    yield start
    for gw in started:
        stop_gateway(gw)


def wait_stderr(gw, pattern: str, timeout_s: float, count: int = 1) -> list[str]:
    """stderr 진단 줄 중 pattern(정규식)을 포함한 줄이 count개가 될 때까지 기다린다."""
    deadline = time.monotonic() + timeout_s
    while True:
        hits = [line for line in list(gw.stderr_lines) if re.search(pattern, line)]
        if len(hits) >= count or time.monotonic() > deadline:
            return hits
        time.sleep(0.05)


def wait_rows(pg, dbname: str, sql: str, n: int, timeout_s: float) -> list[list[str]]:
    deadline = time.monotonic() + timeout_s
    while True:
        rows = pg.psql(sql, dbname)
        if len(rows) >= n or time.monotonic() > deadline:
            return rows
        time.sleep(0.1)


def telemetry(seq: int) -> Telemetry:
    return Telemetry(lat=375_000_000 + seq, lon=1_270_000_000 - seq, heading=seq * 100, speed=150 + seq,
                     battery=90, mode=1, status_flags=0b1011)


def send_telemetry(sock, port: int, seq: int) -> None:
    sock.sendto(encode_frame(MsgType.TELEMETRY, seq, time.time_ns() // 1_000_000, telemetry(seq).to_payload()),
                ("127.0.0.1", port))


def stream_telemetry(port: int, seqs: range, period_s: float = 0.1) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        t0 = time.monotonic()
        for i, seq in enumerate(seqs):
            send_telemetry(sock, port, seq)
            delay = t0 + (i + 1) * period_s - time.monotonic()
            if delay > 0:
                time.sleep(delay)


CAUSES = ("queue_full", "db_down", "write_error")
NO_DROPS = dict.fromkeys(CAUSES, 0)
TOTALS = re.compile(r"total queue_full (\d+), db_down (\d+), write_error (\d+)")


def _totals(line: str) -> dict[str, int]:
    m = TOTALS.search(line)
    assert m, line
    return dict(zip(CAUSES, (int(v) for v in m.groups())))


def wait_drop_report(gw, timeout_s: float = 3.0) -> dict[str, int]:
    """다음 "db log dropped" 1Hz 보고를 기다려 원인별 누계를 돌려준다. 보고가 없으면 실패."""
    before = sum(1 for line in list(gw.stderr_lines) if line.startswith("db log dropped"))
    lines = wait_stderr(gw, r"^db log dropped", timeout_s, count=before + 1)
    assert len(lines) > before, f"버림 보고 없음: {gw.stderr_lines}"
    return _totals(lines[-1])


def closed_totals(gw) -> dict[str, int]:
    """GW를 멈추고 종료 때 남기는 최종 누계(`db log closed`)를 돌려준다. 버림이 0이어도 이 줄은 나온다.
    1Hz 보고 사이에 생긴 버림까지 들어 있어 "버림 0" 판정에 쓴다."""
    stop_gateway(gw)
    lines = wait_stderr(gw, r"^db log closed: ", 3.0)
    assert lines, f"최종 누계 없음: {gw.stderr_lines}"
    return _totals(lines[-1])


class HeartbeatListener:
    """SIM 자리에서 GW의 HEARTBEAT 도착 시각을 모은다 (io 스레드가 멈추지 않았는지 보는 지표)."""

    def __init__(self, port: int) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", port))
        self.sock.settimeout(0.2)
        self.arrivals: list[float] = []
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        while not self._stop:
            try:
                data, _ = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            if data[3] == MsgType.HEARTBEAT:
                self.arrivals.append(time.monotonic())

    def close(self) -> list[float]:
        self._stop = True
        self._thread.join()
        self.sock.close()
        return [(b - a) * 1000 for a, b in zip(self.arrivals, self.arrivals[1:])]


def test_REQ_LOG_001_valid_telemetry_and_detection_are_stored(pg, start_gw):
    """TC-LOG-001-07: 유효 TELEMETRY 20개와 DETECTION 1개가 ICD 정수 값 그대로, IF-03과 같은 rx_ts로 저장된다.
    CRC가 틀린 프레임은 저장하지 않는다."""
    db = pg.new_database()
    gw = start_gw("--db", pg.conninfo(db))
    assert wait_stderr(gw, r"^db connected$", 5.0), gw.stderr_lines

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for seq in range(20):
            send_telemetry(sock, gw.vehicle_port, seq)
            time.sleep(0.02)
        bad = bytearray(encode_frame(MsgType.TELEMETRY, 20, 1, telemetry(20).to_payload()))
        bad[-1] ^= 0xFF  # CRC 오류 → 규칙 ⑤에서 폐기
        sock.sendto(bytes(bad), ("127.0.0.1", gw.vehicle_port))
        time.sleep(0.02)
        send_telemetry(sock, gw.vehicle_port, 21)  # 폐기한 프레임 뒤의 유효 프레임: 이것이 저장되면 앞 프레임도 처리가 끝났다
        head = struct.pack(">IQHHB", 77, 1_800_000_000_000, 1280, 720, 2)
        objs = struct.pack(">BHHHHH", 0, 9500, 10, 20, 30, 40) + struct.pack(">BHHHHH", 255, 1, 0, 0, 1, 1)
        sock.sendto(encode_frame(MsgType.DETECTION, 5, 123, head + objs), ("127.0.0.1", gw.detector_port))

    relayed = gw.messages("telemetry", 21, timeout_s=3.0)
    assert len(relayed) == 21
    rx_by_seq = {m["data"]["seq"]: m["data"]["rx_ts"] for m in relayed}

    sql = "SELECT seq, rx_ts_ms, lat, lon, heading, speed, battery, mode, status_flags FROM telemetry ORDER BY seq"
    assert wait_rows(pg, db, "SELECT 1 FROM telemetry WHERE seq = 21", 1, 5.0)
    rows = pg.psql(sql, db)  # seq 21이 저장된 뒤 다시 센다: CRC 오류 프레임(seq 20)이 늦게 들어올 틈이 없다
    assert [int(r[0]) for r in rows] == [*range(20), 21], rows
    for row in rows:
        seq = int(row[0])
        t = telemetry(seq)
        assert [int(v) for v in row[1:]] == [rx_by_seq[seq], t.lat, t.lon, t.heading, t.speed, t.battery, t.mode,
                                             t.status_flags]

    det = wait_rows(pg, db, "SELECT seq, tx_ts_ms, frame_id, capture_ts_ms, img_w, img_h, objects FROM detection", 1, 3.0)
    assert len(det) == 1, det
    assert det[0][:6] == ["5", "123", "77", "1800000000000", "1280", "720"]
    assert json.loads(det[0][6]) == [{"class_id": 0, "confidence": 9500, "x": 10, "y": 20, "w": 30, "h": 40},
                                     {"class_id": 255, "confidence": 1, "x": 0, "y": 0, "w": 1, "h": 1}]


async def _send_command(ws_port: int, command: dict, timeout_s: float) -> dict | None:
    async with websockets.connect(f"ws://127.0.0.1:{ws_port}/ws") as ws:
        await ws.send(json.dumps({"type": "command", "data": command}))
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                m = json.loads(await asyncio.wait_for(ws.recv(), deadline - time.monotonic()))
            except asyncio.TimeoutError:
                return None
            if m["type"] == "cmd_result":
                return m["data"]
    return None


def test_REQ_LOG_001_link_transitions_and_command_events_are_stored(pg, start_gw):
    """TC-LOG-001-08: 차량 링크 전이 LOST→CONNECTED→DEGRADED→LOST, 명령 송신 4회와 TIMEOUT 결과가 이벤트로 저장된다.
    req_id에 NUL 문자가 있어도 저장이 깨지지 않는다 (U+FFFD로 바꿔 저장)."""
    db = pg.new_database()
    gw = start_gw("--db", pg.conninfo(db))
    assert wait_stderr(gw, r"^db connected$", 5.0), gw.stderr_lines

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for seq in range(3):
            send_telemetry(sock, gw.vehicle_port, seq)
            time.sleep(0.05)
    # 받는 SIM이 없으므로 ACK가 오지 않는다: 4회 송신 뒤 TIMEOUT (ICD 7.3)
    result = asyncio.run(_send_command(gw.ws_port, {"req_id": "a\u0000b", "cmd_type": "STOP"}, 6.0))
    assert result and result["result"] == "TIMEOUT", result

    sql = "SELECT kind, coalesce(source, ''), detail FROM event ORDER BY id"
    rows = wait_rows(pg, db, sql, 3 + 4 + 1, 3.0)
    links = [(r[1], json.loads(r[2])["from"], json.loads(r[2])["to"]) for r in rows if r[0] == "link"]
    vehicle = [(f, t) for s, f, t in links if s == "VEHICLE"]
    assert vehicle == [("LOST", "CONNECTED"), ("CONNECTED", "DEGRADED"), ("DEGRADED", "LOST")], links

    sent = [json.loads(r[2]) for r in rows if r[0] == "cmd_sent"]
    assert [s["attempt"] for s in sent] == [1, 2, 3, 4]
    assert all(s["req_id"] == "a�b" and s["cmd_type"] == "STOP" for s in sent), sent
    assert len({s["cmd_id"] for s in sent}) == 1
    results = [json.loads(r[2]) for r in rows if r[0] == "cmd_result"]
    assert results == [{"req_id": "a�b", "cmd_id": sent[0]["cmd_id"], "result": "TIMEOUT", "attempts": 4,
                        "repeated": False}]
    assert closed_totals(gw) == NO_DROPS


def test_REQ_LOG_003_gateway_relays_without_db_option(gateway):
    """TC-LOG-003-09: --db 없이 띄우면 DB 저장을 끈 채 기동하고 중계한다."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for seq in range(5):
            send_telemetry(sock, gateway.vehicle_port, seq)
    assert len(gateway.messages("telemetry", 5, timeout_s=2.0)) == 5
    assert wait_stderr(gateway, r"^db log: disabled \(no --db\)$", 1.0)
    assert not wait_stderr(gateway, r"^db ", 0.5, count=2)[1:]  # 그 밖의 DB 진단(접속 시도·버림)이 없다


def test_REQ_LOG_003_unreachable_db_does_not_stop_relay_and_drops_are_counted(start_gw, tmp_path):
    """TC-LOG-003-10: 없는 DB를 가리켜도 GW는 기동해 중계·HEARTBEAT를 계속하고, 버린 기록을 db_down으로 센다.
    재접속 실패는 처음 한 번만 알린다 (PostgreSQL 서버가 필요 없는 시험)."""
    gw = start_gw("--db", f"host={tmp_path} port=5432 dbname=none")
    hb = HeartbeatListener(gw.sim_port)
    try:
        stream_telemetry(gw.vehicle_port, range(30))
        assert len(gw.messages("telemetry", 30, timeout_s=2.0)) == 30
        time.sleep(2.5)  # 재접속 시도 0.5s·1s·2s 간격 + 1Hz 보고
    finally:
        intervals = hb.close()
    assert len(wait_stderr(gw, r"^db unavailable: ", 0.1)) == 1, gw.stderr_lines
    totals = closed_totals(gw)
    assert totals["db_down"] >= 30 + 1, (totals, gw.stderr_lines)  # TELEMETRY 30 + 링크 전이
    assert totals["queue_full"] == totals["write_error"] == 0
    assert len(intervals) >= 4 and max(intervals) <= 1100, intervals


def test_REQ_LOG_003_db_restart_keeps_relay_and_reconnects(pg, start_gw):
    """TC-LOG-003-11: 10Hz 중계 중 DB를 내렸다가 2.5s 뒤 다시 올린다. 중계·HEARTBEAT는 끊기지 않고,
    끊긴 동안의 기록은 원인별로 세며, GW가 스스로 다시 접속해 이후 TELEMETRY를 저장한다."""
    db = pg.new_database()
    gw = start_gw("--db", pg.conninfo(db))
    assert wait_stderr(gw, r"^db connected$", 5.0), gw.stderr_lines
    hb = HeartbeatListener(gw.sim_port)
    sender = threading.Thread(target=stream_telemetry, args=(gw.vehicle_port, range(90)))
    try:
        sender.start()
        time.sleep(1.0)
        pg.stop("fast")
        time.sleep(2.5)
        pg.start()
        sender.join()
        relayed = gw.messages("telemetry", 90, timeout_s=2.0)
        assert wait_stderr(gw, r"^db connected$", 8.0, count=2)[1:], gw.stderr_lines
        stream_telemetry(gw.vehicle_port, range(100, 105))  # 다시 접속한 뒤 보낸 기록
        assert len(gw.messages("telemetry", 5, timeout_s=2.0)) == 5
    finally:
        intervals = hb.close()
        if sender.is_alive():
            sender.join()
    assert [m["data"]["seq"] for m in relayed] == list(range(90))
    assert max(intervals) <= 1100, intervals
    assert wait_stderr(gw, r"^db (write failed|unavailable)", 0.1), gw.stderr_lines

    rows = wait_rows(pg, db, "SELECT seq FROM telemetry WHERE seq >= 100", 5, 3.0)
    assert [int(r[0]) for r in rows] == list(range(100, 105))  # 다시 접속한 뒤 보낸 기록은 모두 저장
    stored = [int(r[0]) for r in pg.psql("SELECT seq FROM telemetry WHERE seq < 90 ORDER BY seq", db)]
    assert stored and stored[0] < 10, stored  # 내리기 전
    assert len(stored) < 90  # 내려가 있던 동안은 버렸다
    totals = closed_totals(gw)
    assert totals["db_down"] + totals["write_error"] >= 90 - len(stored), (totals, stored)  # 잃은 TELEMETRY는 모두 셌다
    assert totals["queue_full"] == 0


def test_REQ_LOG_003_hung_db_does_not_delay_relay(pg, start_gw):
    """TC-LOG-003-12: GW를 맡은 DB 서버 프로세스를 3s 멈춰도(SIGSTOP) 중계·HEARTBEAT가 그대로 가고,
    풀리면 대기열에 있던 기록까지 모두 저장된다 (대기열 상한 안이라 버림 0)."""
    db = pg.new_database()
    gw = start_gw("--db", pg.conninfo(db))
    assert wait_stderr(gw, r"^db connected$", 5.0), gw.stderr_lines
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        send_telemetry(sock, gw.vehicle_port, 0)
    assert wait_rows(pg, db, "SELECT seq FROM telemetry", 1, 3.0)
    pids = pg.gateway_backend_pids()
    assert pids

    hb = HeartbeatListener(gw.sim_port)
    gw.drain()
    pg.freeze(pids)
    try:
        t0 = time.monotonic()
        stream_telemetry(gw.vehicle_port, range(1, 31))
        relayed = gw.messages("telemetry", 30, timeout_s=1.0)
        elapsed = time.monotonic() - t0
    finally:
        pg.thaw(pids)
        intervals = hb.close()
    assert [m["data"]["seq"] for m in relayed] == list(range(1, 31))
    assert elapsed < 3.6, elapsed  # 송신 3.0s + 여유. DB를 기다렸다면 더 걸린다
    assert len(intervals) >= 2 and max(intervals) <= 1100, intervals

    rows = wait_rows(pg, db, "SELECT seq FROM telemetry ORDER BY seq", 31, 5.0)
    assert [int(r[0]) for r in rows] == list(range(31))
    assert closed_totals(gw) == NO_DROPS


def test_REQ_LOG_003_failed_record_does_not_cost_other_records(pg, start_gw):
    """TC-LOG-003-15: 서버 인코딩이 EUC_KR인 DB. 한글 req_id 명령은 그대로 저장되고(접속 인코딩 UTF8),
    EUC_KR로 바꿀 수 없는 문자(이모지)가 든 req_id 명령의 이벤트만 write_error로 버린다.
    같은 시간에 보낸 TELEMETRY는 모두 저장된다 (실패한 행만 잃는다, PR #50 리뷰 1·2차)."""
    db = pg.new_database(encoding="EUC_KR")
    gw = start_gw("--db", pg.conninfo(db))
    assert wait_stderr(gw, r"^db connected$", 5.0), gw.stderr_lines

    async def send_both():
        async with websockets.connect(f"ws://127.0.0.1:{gw.ws_port}/ws") as ws:
            for req_id in ("한글명령", "x\U0001F600"):
                await ws.send(json.dumps({"type": "command", "data": {"req_id": req_id, "cmd_type": "STOP"}}))

    sender = threading.Thread(target=stream_telemetry, args=(gw.vehicle_port, range(30)))
    sender.start()
    time.sleep(0.5)
    asyncio.run(send_both())  # 받는 SIM이 없어 1.0s마다 재전송: 저장 중인 TELEMETRY 사이에 이벤트가 섞인다
    sender.join()

    rows = wait_rows(pg, db, "SELECT seq FROM telemetry ORDER BY seq", 30, 5.0)
    assert [int(r[0]) for r in rows] == list(range(30))
    sent = pg.psql("SELECT detail->>'req_id' FROM event WHERE kind = 'cmd_sent'", db)
    assert sent and {r[0] for r in sent} == {"한글명령"}, sent
    assert wait_stderr(gw, r"^db write failed, dropped 1 record: ", 0.1), gw.stderr_lines

    totals = closed_totals(gw)
    assert totals["write_error"] >= 2, gw.stderr_lines  # 이모지 명령의 송신·재전송 이벤트
    assert totals["db_down"] == totals["queue_full"] == 0, gw.stderr_lines


def test_REQ_LOG_003_dropped_table_is_recreated_by_reconnecting(pg, start_gw):
    """TC-LOG-003-16: 10Hz 저장 중에 다른 세션이 telemetry 테이블을 지운다. 그 순간 저장하던 행만 잃고,
    GW가 다시 접속하며 테이블을 다시 만들어 이후 TELEMETRY를 모두 저장한다 (PR #50 2차 리뷰 1, 수정 전 2차 코드는
    테이블이 없는 채로 50건을 모두 잃었다)."""
    db = pg.new_database()
    gw = start_gw("--db", pg.conninfo(db))
    assert wait_stderr(gw, r"^db connected$", 5.0), gw.stderr_lines
    sender = threading.Thread(target=stream_telemetry, args=(gw.vehicle_port, range(50)))
    sender.start()
    time.sleep(1.5)
    # 잠금을 잡은 채 지운 행 수를 따로 남기고 지운다: 지우기 전에 저장된 행을 정확히 센다
    pg.psql("LOCK TABLE telemetry IN ACCESS EXCLUSIVE MODE; "
            "CREATE TABLE before_drop AS SELECT count(*) AS n FROM telemetry; DROP TABLE telemetry", db)
    sender.join()
    assert len(gw.messages("telemetry", 50, timeout_s=2.0)) == 50

    before = int(pg.psql("SELECT n FROM before_drop", db)[0][0])
    after = [int(r[0]) for r in wait_rows(pg, db, "SELECT seq FROM telemetry WHERE seq = 49", 1, 5.0)]
    assert after == [49], gw.stderr_lines
    after = [int(r[0]) for r in pg.psql("SELECT seq FROM telemetry ORDER BY seq", db)]
    assert after == list(range(after[0], 50)), after  # 다시 만든 뒤로 빠짐없이
    assert 5 <= before < 50
    totals = closed_totals(gw)
    assert totals["db_down"] == totals["queue_full"] == 0, gw.stderr_lines
    assert before + totals["write_error"] + len(after) == 50, (before, totals, after)  # 잃은 것은 실패한 행뿐
    assert totals["write_error"] <= 2, totals
