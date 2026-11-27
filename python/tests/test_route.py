"""ugvc_proto.route 시험. 벡터는 C++ gateway 시험과 같은 파일이다."""
import json

import pytest

from conftest import VECTORS_DIR
from ugvc_proto.route import MAX_WAYPOINTS, Route

ROUTES = json.loads((VECTORS_DIR / "route.json").read_text(encoding="utf-8"))["vectors"]


@pytest.mark.parametrize("v", ROUTES, ids=[v["name"] for v in ROUTES])
def test_REQ_CMD_007_route_matches_vector(v):
    r = Route(tuple((w["lat"], w["lon"]) for w in v["fields"]["waypoints"]))
    assert r.to_payload().hex() == v["payload_hex"]
    assert Route.from_payload(bytes.fromhex(v["payload_hex"])) == r


def test_REQ_CMD_007_route_length_must_match_count():
    one = Route(((1, 2),)).to_payload()
    assert Route.from_payload(b"") is None
    assert Route.from_payload(b"\x02" + one[1:]) is None  # count 2인데 경로점 1개
    assert Route.from_payload(one + b"\x00") is None
    assert len(Route(((0, 0),) * MAX_WAYPOINTS).to_payload()) == 257
