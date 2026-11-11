import json
from pathlib import Path

import pytest

from ugvc_proto.command import Ack, AckResult, Command, CmdType
from ugvc_proto.frame import MsgType, encode_frame

from ugvc_sim import uplink
from ugvc_sim.uplink import Heartbeat, HeartbeatWatch, ReceivedCommand, parse_uplink

VECTORS = Path(__file__).resolve().parents[2] / "testdata" / "vectors"
COMMANDS = json.loads((VECTORS / "command.json").read_text(encoding="utf-8"))["vectors"]


def test_REQ_COM_007_parses_heartbeat():
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
def test_REQ_COM_007_rejects_bad_frames(data, reason):
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


@pytest.mark.parametrize("name, command", [
    ("cmd_id_0", Command(0, CmdType.STOP)),
    ("cmd_type_undefined", Command(2, 0x99)),
    ("waypoint_lat_over", Command(3, CmdType.WAYPOINT, 900_000_001, 0)),
    ("waypoint_lon_under", Command(4, CmdType.WAYPOINT, 0, -1_800_000_001)),
])
def test_REQ_CMD_006_rejects_out_of_range_command(name, command):
    assert parse_uplink(encode_frame(MsgType.COMMAND, 0, 0, command.to_payload())) == "range"


def test_REQ_CMD_006_waypoint_bounds_accepted():
    for lat, lon in [(900_000_000, 1_800_000_000), (-900_000_000, -1_800_000_000)]:
        msg = parse_uplink(encode_frame(MsgType.COMMAND, 0, 0, Command(9, CmdType.WAYPOINT, lat, lon).to_payload()))
        assert isinstance(msg, ReceivedCommand)


def test_REQ_COM_007_rejects_datagram_over_1200():
    assert parse_uplink(encode_frame(MsgType.HEARTBEAT, 0, 0) + bytes(1181)) == "length"


@pytest.mark.xfail(
    strict=True,
    raises=AttributeError,
    reason="REQ-CMD-006 ACK 응답은 12월 명령 작업에서 구현. 구현하면 이 xfail 표시를 지운다",
)
@pytest.mark.parametrize("name, command, expected", [
    ("cmd_type_undefined", Command(2, 0x99), Ack(2, AckResult.REJECTED_INVALID)),
    ("waypoint_lat_over", Command(3, CmdType.WAYPOINT, 900_000_001, 0), Ack(3, AckResult.REJECTED_INVALID)),
    ("waypoint_lon_under", Command(4, CmdType.WAYPOINT, 0, -1_800_000_001), Ack(4, AckResult.REJECTED_INVALID)),
    ("cmd_id_0_no_ack", Command(0, CmdType.STOP), None),
])
def test_REQ_CMD_006_acks_rejected_invalid(name, command, expected):
    """범위 밖 명령은 실행하지 않고 REJECTED_INVALID로 ACK한다. cmd_id 0은 응답할 대상이 없어 ACK가 없다.

    아직 구현 전이라 `uplink.ack_for_command`가 없어 AttributeError로 실패한다(xfail).
    함수가 생긴 뒤 결과가 틀리면 AttributeError가 아니므로 일반 실패로 드러나고,
    맞으면 strict=True 때문에 XPASS 실패로 드러나 xfail 표시를 지우라고 알린다.
    """
    frame = encode_frame(MsgType.COMMAND, 0, 0, command.to_payload())
    assert uplink.ack_for_command(frame) == expected
