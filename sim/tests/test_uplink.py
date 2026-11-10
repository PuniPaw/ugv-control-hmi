import json
from pathlib import Path

import pytest

from ugvc_proto.command import Command, CmdType
from ugvc_proto.frame import MsgType, encode_frame

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
def test_ICD_6_3_rejects_out_of_range_command(name, command):
    assert parse_uplink(encode_frame(MsgType.COMMAND, 0, 0, command.to_payload())) == "range"


def test_ICD_6_3_waypoint_bounds_accepted():
    for lat, lon in [(900_000_000, 1_800_000_000), (-900_000_000, -1_800_000_000)]:
        msg = parse_uplink(encode_frame(MsgType.COMMAND, 0, 0, Command(9, CmdType.WAYPOINT, lat, lon).to_payload()))
        assert isinstance(msg, ReceivedCommand)


def test_REQ_COM_007_rejects_datagram_over_1200():
    assert parse_uplink(encode_frame(MsgType.HEARTBEAT, 0, 0) + bytes(1181)) == "length"
