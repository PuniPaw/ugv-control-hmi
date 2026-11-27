"""SIM 시험 공용 도우미: SIM이 GW로 보낸 프레임 해석. (conftest라는 이름은 python/tests와 겹쳐 한꺼번에 수집할 때 충돌한다)"""
from ugvc_proto.command import ACK_SIZE, Ack
from ugvc_proto.frame import Header, MsgType, check_frame, decode_header
from ugvc_proto.route import Route
from ugvc_proto.telemetry import PAYLOAD_SIZE, Telemetry


def parse_downlink(data: bytes) -> tuple[Header, Telemetry | Ack | Route]:
    """SIM이 GW 차량 포트로 보낸 프레임 → (헤더, 메시지). 규칙 ①~⑦을 통과하지 않으면 AssertionError.

    ROUTE 길이는 count로 정해지므로(ICD 9장 ⑦) 헤더 길이로 프레임을 통과시킨 뒤 Route.from_payload가 count와 맞춰 본다.
    """
    header = decode_header(data)
    lengths = {MsgType.TELEMETRY: PAYLOAD_SIZE, MsgType.ACK: ACK_SIZE, MsgType.ROUTE: header.payload_len}
    checked = check_frame(data, lengths)
    assert isinstance(checked, tuple), checked
    header, payload = checked
    decode = {MsgType.TELEMETRY: Telemetry.from_payload, MsgType.ACK: Ack.from_payload,
              MsgType.ROUTE: Route.from_payload}[header.msg_type]
    message = decode(payload)
    assert message is not None, header
    return header, message
