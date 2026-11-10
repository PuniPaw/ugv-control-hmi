"""ugvc_proto.telemetry 시험. C++ gateway/tests/codec/telemetry_test.cpp와 같은 벡터를 쓴다."""
import json

import pytest

from conftest import VECTORS_DIR
from ugvc_proto.telemetry import Flags, Mode, Telemetry

VECTORS = json.loads((VECTORS_DIR / "telemetry.json").read_text(encoding="utf-8"))["vectors"]
BY_NAME = {v["name"]: v for v in VECTORS}


def _decode(name: str) -> Telemetry:
    t = Telemetry.from_payload(bytes.fromhex(BY_NAME[name]["payload_hex"]))
    assert t is not None
    return t


# TC-COM-001-01
def test_REQ_COM_001_decode_telemetry():
    t = _decode("telemetry_nominal")
    assert t == Telemetry(**BY_NAME["telemetry_nominal"]["fields"])
    assert t.mode == Mode.MISSION
    assert t.status_flags == Flags.GPS_VALID | Flags.CAMERA_OK


# TC-COM-001-02
@pytest.mark.parametrize("name", ["telemetry_bounds_min", "telemetry_bounds_max"])
def test_REQ_COM_001_decode_bounds(name):
    assert _decode(name) == Telemetry(**BY_NAME[name]["fields"])


# TC-COM-001-03: Python 인코딩 == 벡터 (== C++ 인코딩)
@pytest.mark.parametrize("v", VECTORS, ids=[v["name"] for v in VECTORS])
def test_REQ_COM_001_encode_matches_vector(v):
    assert Telemetry(**v["fields"]).to_payload().hex() == v["payload_hex"]


# TC-COM-001-04의 codec 부분
def test_REQ_COM_001_reserved_bits_preserved_by_decoder():
    assert _decode("telemetry_reserved_bits").status_flags == 0xFFF3


def test_REQ_COM_002_telemetry_wrong_length():
    assert Telemetry.from_payload(bytes(15)) is None
    assert Telemetry.from_payload(bytes(17)) is None
