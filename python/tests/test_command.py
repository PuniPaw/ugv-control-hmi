"""ugvc_proto.command 시험. 벡터는 C++ gateway 시험과 같은 파일이다."""
import json

import pytest

from conftest import VECTORS_DIR
from ugvc_proto.command import Ack, Command

COMMANDS = json.loads((VECTORS_DIR / "command.json").read_text(encoding="utf-8"))["vectors"]
ACKS = json.loads((VECTORS_DIR / "ack.json").read_text(encoding="utf-8"))["vectors"]


@pytest.mark.parametrize("v", COMMANDS, ids=[v["name"] for v in COMMANDS])
def test_REQ_QUAL_001_command_matches_vector(v):
    c = Command(**v["fields"])
    assert c.to_payload().hex() == v["payload_hex"]
    assert Command.from_payload(bytes.fromhex(v["payload_hex"])) == c


@pytest.mark.parametrize("v", ACKS, ids=[v["name"] for v in ACKS])
def test_REQ_QUAL_001_ack_matches_vector(v):
    a = Ack(**v["fields"])
    assert a.to_payload().hex() == v["payload_hex"]
    assert Ack.from_payload(bytes.fromhex(v["payload_hex"])) == a


def test_wrong_length_returns_none():
    assert Command.from_payload(bytes(12)) is None
    assert Ack.from_payload(bytes(6)) is None
