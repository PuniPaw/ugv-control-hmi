"""SIM(Python) → GW(C++) 연결 확인: Python이 보낸 바이트를 C++가 같은 값으로 해석하는가 (ADR-002)."""
import socket
import time

from ugvc_proto.frame import encode_frame
from ugvc_proto.telemetry import MSG_TELEMETRY, Telemetry

from ugvc_sim.telemetry_sender import START
from ugvc_sim.vehicle import step, to_telemetry

N = 30


def test_sim_telemetry_decoded_identically_by_gateway(gateway):
    sent: list[Telemetry] = []
    state = START
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        for seq in range(N):
            t = to_telemetry(state)
            sent.append(t)
            sock.sendto(encode_frame(MSG_TELEMETRY, seq, time.time_ns() // 1_000_000, t.to_payload()),
                        ("127.0.0.1", gateway.vehicle_port))
            state = step(state, 0.1)
            time.sleep(0.01)

    msgs = gateway.messages("telemetry", N, timeout_s=5.0)
    assert len(msgs) == N
    for seq, (msg, t) in enumerate(zip(msgs, sent)):
        d = msg["data"]
        assert d["seq"] == seq
        # GW는 정수 LSB × 배율로 공학 단위를 만든다 → 다시 LSB로 돌리면 송신값과 같아야 한다
        assert round(d["lat"] * 1e7) == t.lat
        assert round(d["lon"] * 1e7) == t.lon
        assert round(d["heading_deg"] * 100) == t.heading
        assert round(d["speed_mps"] * 100) == t.speed
        assert d["battery_pct"] == t.battery
        assert d["mode"] == "MISSION"
        assert d["flags"] == ["GPS_VALID", "CAMERA_OK"]


def test_gateway_rejects_corrupted_crc_and_counts_it(gateway):
    good = encode_frame(MSG_TELEMETRY, 0, 0, to_telemetry(START).to_payload())
    bad = good[:-1] + bytes([good[-1] ^ 0x01])
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(bad, ("127.0.0.1", gateway.vehicle_port))
        sock.sendto(good, ("127.0.0.1", gateway.vehicle_port))
    msgs = gateway.messages("telemetry", 1, timeout_s=3.0)
    assert len(msgs) == 1 and msgs[0]["data"]["seq"] == 0
    stats = gateway.messages("stats", 1, timeout_s=3.0,
                             pred=lambda m: m["data"]["source"] == "VEHICLE" and m["data"]["rx_count"] == 1)
    assert stats and stats[0]["data"]["errors"]["crc"] == 1


def test_link_goes_lost_after_packets_stop(gateway):
    """REQ-COM-003·004를 실제 프로세스로: 패킷이 멈추면 DEGRADED → LOST 전이가 출력된다."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(encode_frame(MSG_TELEMETRY, 0, 0, to_telemetry(START).to_payload()),
                    ("127.0.0.1", gateway.vehicle_port))
    vehicle = lambda m: m["data"]["source"] == "VEHICLE"
    # 연속 중복을 뺀 상태 열. 시작 시 1Hz 보고의 LOST(첫 패킷 전)가 앞에 올 수 있다.
    states: list[str] = []
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline and states[-3:] != ["CONNECTED", "DEGRADED", "LOST"]:
        for m in gateway.messages("link_status", 1, timeout_s=0.5, pred=vehicle):
            if not states or states[-1] != m["data"]["state"]:
                states.append(m["data"]["state"])
    assert states[-3:] == ["CONNECTED", "DEGRADED", "LOST"], states
