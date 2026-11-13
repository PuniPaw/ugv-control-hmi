import json
import socket
import threading
from pathlib import Path

import pytest

from ugvc_proto.command import Ack, AckResult, Command, CmdType
from ugvc_proto.frame import MsgType, check_frame, encode_frame

from ugvc_sim import uplink
from ugvc_sim.uplink import CommandResponder, Heartbeat, HeartbeatWatch, InvalidCommand, ReceivedCommand, parse_uplink

VECTORS = Path(__file__).resolve().parents[2] / "testdata" / "vectors"
COMMANDS = json.loads((VECTORS / "command.json").read_text(encoding="utf-8"))["vectors"]


def test_REQ_SAFE_001_parses_heartbeat():
    assert parse_uplink(encode_frame(MsgType.HEARTBEAT, 7, 1000)) == Heartbeat(7, 1000)


@pytest.mark.parametrize("v", COMMANDS, ids=[v["name"] for v in COMMANDS])
def test_REQ_QUAL_001_parses_command_vectors(v):
    frame = encode_frame(MsgType.COMMAND, 3, 2000, bytes.fromhex(v["payload_hex"]))
    assert parse_uplink(frame) == ReceivedCommand(3, 2000, Command(**v["fields"]))


def test_REQ_QUAL_001_waypoint_fields():
    msg = parse_uplink(encode_frame(MsgType.COMMAND, 0, 0, Command(5, CmdType.WAYPOINT, 361240000, 1277660000).to_payload()))
    assert isinstance(msg, ReceivedCommand)
    assert msg.command.cmd_type == CmdType.WAYPOINT and msg.command.lat == 361240000


@pytest.mark.parametrize("data, reason", [
    (b"", "length"),
    (b"\x55\xaa" + encode_frame(MsgType.HEARTBEAT, 0, 0)[2:], "sync"),
    (encode_frame(MsgType.HEARTBEAT, 0, 0)[:-1] + b"\x00", "crc"),
    (encode_frame(MsgType.TELEMETRY, 0, 0, bytes(16)), "type"),     # SIM은 TELEMETRY를 받지 않는다
    (encode_frame(MsgType.COMMAND, 0, 0, bytes(12)), "length"),      # COMMAND는 13B
    (encode_frame(MsgType.HEARTBEAT, 0, 0, b"\x00"), "length"),      # HEARTBEAT는 0B
])
def test_uplink_rejects_bad_frames(data, reason):
    assert parse_uplink(data) == reason


def test_REQ_SAFE_001_heartbeat_watch_3s():
    w = HeartbeatWatch(start=90.0)
    w.on_heartbeat(100.0)
    assert not w.expired(102.999)
    assert w.expired(103.0)              # 3.0초 이상
    w.on_heartbeat(103.5)
    assert not w.expired(104.0)


def test_REQ_SAFE_001_never_received_expires_from_start():
    """한 번도 못 받은 것도 미수신이다: 시작 후 3.0초가 지나면 만료 (안전 쪽 기본값)."""
    w = HeartbeatWatch(start=10.0)
    assert not w.expired(12.999)
    assert w.expired(13.0)


def _frame(command, seq=0):
    return encode_frame(MsgType.COMMAND, seq, 0, command.to_payload())


INVALID = [
    ("cmd_type_undefined", Command(2, 0x99), Ack(2, AckResult.REJECTED_INVALID)),
    ("waypoint_lat_over", Command(3, CmdType.WAYPOINT, 900_000_001, 0), Ack(3, AckResult.REJECTED_INVALID)),
    ("waypoint_lon_under", Command(4, CmdType.WAYPOINT, 0, -1_800_000_001), Ack(4, AckResult.REJECTED_INVALID)),
]


def test_REQ_CMD_006_cmd_id_0_is_dropped():
    assert parse_uplink(_frame(Command(0, CmdType.STOP))) == "range"


@pytest.mark.parametrize("name, command, expected", INVALID, ids=[i[0] for i in INVALID])
def test_REQ_CMD_006_out_of_range_command_is_parsed_as_invalid(name, command, expected):
    assert parse_uplink(_frame(command)) == InvalidCommand(0, 0, command)


def test_REQ_CMD_006_waypoint_bounds_accepted():
    for lat, lon in [(900_000_000, 1_800_000_000), (-900_000_000, -1_800_000_000)]:
        msg = parse_uplink(encode_frame(MsgType.COMMAND, 0, 0, Command(9, CmdType.WAYPOINT, lat, lon).to_payload()))
        assert isinstance(msg, ReceivedCommand)


def test_uplink_rejects_datagram_over_1200():
    assert parse_uplink(encode_frame(MsgType.HEARTBEAT, 0, 0) + bytes(1181)) == "length"


@pytest.mark.parametrize("name, command, expected", INVALID + [("cmd_id_0_no_ack", Command(0, CmdType.STOP), None)],
                         ids=[i[0] for i in INVALID] + ["cmd_id_0_no_ack"])
def test_REQ_CMD_006_acks_rejected_invalid(name, command, expected):
    """범위 밖 명령은 실행하지 않고 REJECTED_INVALID로 ACK한다. cmd_id 0은 응답할 대상이 없어 ACK가 없다."""
    assert uplink.ack_for_command(_frame(command)) == expected


def test_REQ_CMD_006_invalid_command_is_not_executed():
    r = CommandResponder()
    for _, command, expected in INVALID:
        out = r.on_command(parse_uplink(_frame(command)))
        assert out.ack == expected and out.execute is None


def test_REQ_CMD_006_valid_command_accepted_and_executed():
    r = CommandResponder()
    for command in [Command(1, CmdType.STOP), Command(2, CmdType.RTB),
                    Command(3, CmdType.WAYPOINT, 900_000_000, 1_800_000_000),
                    Command(4, CmdType.WAYPOINT, -900_000_000, -1_800_000_000)]:
        out = r.on_command(parse_uplink(_frame(command)))
        assert out.ack == Ack(command.cmd_id, AckResult.ACCEPTED) and out.execute == command


def test_REQ_CMD_003_duplicate_cmd_id_resends_same_ack_without_executing():
    r = CommandResponder()
    msg = parse_uplink(_frame(Command(7, CmdType.STOP)))
    first = r.on_command(msg)
    second = r.on_command(msg)
    assert first.execute is not None
    assert second.ack == first.ack == Ack(7, AckResult.ACCEPTED)
    assert second.execute is None


def test_REQ_CMD_003_duplicate_of_rejected_command_resends_rejection():
    r = CommandResponder()
    msg = parse_uplink(_frame(Command(8, 0x99)))
    assert r.on_command(msg).ack == r.on_command(msg).ack == Ack(8, AckResult.REJECTED_INVALID)


def test_REQ_CMD_003_different_cmd_ids_are_independent():
    r = CommandResponder()
    a = r.on_command(parse_uplink(_frame(Command(1, CmdType.STOP))))
    b = r.on_command(parse_uplink(_frame(Command(2, CmdType.RTB))))
    assert a.execute is not None and b.execute is not None
    assert a.ack.cmd_id == 1 and b.ack.cmd_id == 2


def test_REQ_CMD_003_memory_is_bounded_and_forgets_oldest():
    r = CommandResponder(memory=3)
    for i in (1, 2, 3, 4):
        r.on_command(parse_uplink(_frame(Command(i, CmdType.STOP))))
    assert r.on_command(parse_uplink(_frame(Command(4, CmdType.STOP)))).execute is None  # 최근: 기억함
    assert r.on_command(parse_uplink(_frame(Command(1, CmdType.STOP)))).execute is not None  # 가장 오래됨: 잊음


def test_REQ_CMD_003_ack_frame_is_valid_and_seq_increments(sim):
    ack_sock, send = sim()
    send(_frame(Command(1, CmdType.STOP)))
    send(_frame(Command(2, CmdType.STOP)))
    frames = [ack_sock.recvfrom(2048)[0] for _ in range(2)]
    checked = [check_frame(f, {MsgType.ACK: 5}) for f in frames]
    assert [c[0].msg_type for c in checked] == [MsgType.ACK] * 2
    assert [len(c[1]) for c in checked] == [5, 5]
    assert [c[0].seq for c in checked] == [0, 1]
    assert [Ack.from_payload(c[1]) for c in checked] == [Ack(1, AckResult.ACCEPTED), Ack(2, AckResult.ACCEPTED)]


@pytest.fixture
def sim():
    """실제 UDP 소켓의 SIM 서버 스레드와 가짜 GW 소켓. sim(drop_first=0)이 (GW 소켓, COMMAND 송신 함수)를 준다."""
    opened, threads, stop = [], [], threading.Event()

    def start(drop_first=0):
        gw = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        gw.bind(("127.0.0.1", 0))
        gw.settimeout(0.5)
        srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        srv.bind(("127.0.0.1", 0))
        opened.extend([gw, srv])
        t = threading.Thread(target=uplink.serve, args=(srv, gw.getsockname(), drop_first, stop), daemon=True)
        t.start()
        threads.append(t)
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        opened.append(client)
        return gw, lambda data: client.sendto(data, srv.getsockname())

    yield start
    stop.set()
    for t in threads:
        t.join(timeout=2)
    for s in opened:
        s.close()


def test_REQ_CMD_003_listen_duplicate_gets_same_ack(sim, capsys):
    gw, send = sim()
    send(_frame(Command(5, CmdType.RTB)))
    send(_frame(Command(5, CmdType.RTB)))
    acks = [Ack.from_payload(check_frame(gw.recvfrom(2048)[0], {MsgType.ACK: 5})[1]) for _ in range(2)]
    assert acks == [Ack(5, AckResult.ACCEPTED)] * 2
    lines = [json.loads(l) for l in capsys.readouterr().out.splitlines() if '"command"' in l]
    assert [l["executed"] for l in lines] == [True, False]


def test_REQ_CMD_006_listen_rejects_invalid_over_udp(sim):
    gw, send = sim()
    send(_frame(Command(6, 0x99)))
    assert Ack.from_payload(check_frame(gw.recvfrom(2048)[0], {MsgType.ACK: 5})[1]) == Ack(6, AckResult.REJECTED_INVALID)


def test_REQ_CMD_006_listen_cmd_id_0_gets_no_ack(sim):
    gw, send = sim()
    send(_frame(Command(0, CmdType.STOP)))
    with pytest.raises(socket.timeout):
        gw.recvfrom(2048)


def test_REQ_CMD_003_drop_first_ignores_first_command_then_acks_retransmission(sim):
    gw, send = sim(drop_first=1)
    send(_frame(Command(9, CmdType.STOP)))
    with pytest.raises(socket.timeout):
        gw.recvfrom(2048)
    send(_frame(Command(9, CmdType.STOP)))
    assert Ack.from_payload(check_frame(gw.recvfrom(2048)[0], {MsgType.ACK: 5})[1]) == Ack(9, AckResult.ACCEPTED)
