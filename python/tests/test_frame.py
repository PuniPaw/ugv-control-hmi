"""ugvc_proto.frame 시험. C++ gateway/tests/codec/frame_test.cpp와 같은 벡터를 쓴다 (STP 2.4)."""
from ugvc_proto.frame import (
    FRAME_OVERHEAD,
    SYNC,
    VERSION,
    crc16_ccitt_false,
    decode_header,
    encode_frame,
    read_frame_crc,
)


# TC-COM-002-02와 같은 표준 벡터
def test_REQ_COM_002_standard_vector():
    assert crc16_ccitt_false(b"123456789") == 0x29B1


def test_REQ_COM_002_vector_file_crc_check(frame_vectors):
    c = frame_vectors["crc_check"]
    assert crc16_ccitt_false(c["ascii"].encode()) == int(c["crc16"], 16)


# ADR-002: Python 인코딩이 벡터(=C++ 인코딩)와 바이트 단위로 같다
def test_REQ_QUAL_001_encode_matches_vectors(frame_vectors):
    for v in frame_vectors["vectors"]:
        frame = encode_frame(v["msg_type"], v["seq"], v["timestamp_ms"], bytes.fromhex(v["payload_hex"]))
        assert frame.hex() == v["hex"], v["name"]


def test_REQ_QUAL_001_decode_header_matches_vectors(frame_vectors):
    for v in frame_vectors["vectors"]:
        data = bytes.fromhex(v["hex"])
        h = decode_header(data)
        assert h is not None, v["name"]
        assert (h.sync, h.version, h.msg_type, h.seq, h.timestamp_ms) == (
            SYNC, VERSION, v["msg_type"], v["seq"], v["timestamp_ms"]), v["name"]
        assert h.payload_len == len(bytes.fromhex(v["payload_hex"])), v["name"]
        assert len(data) == FRAME_OVERHEAD + h.payload_len, v["name"]


def test_REQ_COM_002_crc_field_covers_header_and_payload(frame_vectors):
    for v in frame_vectors["vectors"]:
        data = bytes.fromhex(v["hex"])
        assert read_frame_crc(data) == crc16_ccitt_false(data[:-2]), v["name"]


def test_REQ_COM_002_short_input_returns_none():
    assert decode_header(bytes(17)) is None
    assert decode_header(bytes(19)) is not None
    assert read_frame_crc(bytes(19)) is None


# 공통 검증 ①~⑦: GW와 같은 validation.json으로, 받을 type·길이를 GW와 같게 주면 ①~⑦ 판정이 같아야 한다
def test_REQ_COM_002_check_frame_matches_gateway_rules_1_to_7():
    import json
    from conftest import VECTORS_DIR
    from ugvc_proto.frame import MsgType, Reject, check_frame

    vehicle_port = {MsgType.TELEMETRY: 16, MsgType.ACK: 5}  # GW 15001이 받는 type과 길이
    cases = json.loads((VECTORS_DIR / "validation.json").read_text(encoding="utf-8"))["cases"]
    checked = 0
    for c in cases:
        if c["source"] != "VEHICLE" or c["expect"] == "range":
            continue  # ⑧ 범위는 이 함수 밖. DETECTION은 길이가 count로 정해져 고정 길이 표로 표현하지 않는다
        r = check_frame(bytes.fromhex(c["hex"]), vehicle_port)
        got = r.value if isinstance(r, Reject) else "accept"
        assert got == c["expect"], c["name"]
        checked += 1
    assert checked >= 12
