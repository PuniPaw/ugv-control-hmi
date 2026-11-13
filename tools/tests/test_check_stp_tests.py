"""check_stp_tests.py 자체 시험: 형식이 틀린 REQ ID 판정, STP 표 해석.

실행: python3 -m pytest -q tools/tests
(제품 시험이 아니므로 STP·RTM 대상이 아니고, REQ ID를 달지 않는다)
"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_stp_tests as c  # noqa: E402

AREAS = ("AI", "CMD", "COM", "HMI", "LOG", "PERF", "QUAL", "SAFE")  # 시험은 실제 SRS 내용에 묶이지 않는다


@pytest.mark.parametrize("name", [
    "REQ_HMI_06 short", "REQ_HMI_0067 long", "REQ HMI 006 spaces", "req_hmi_999 lower",
    "REQHMI006 no separator", "REQ.HMI.006 dots", "REQ_HMI_O06 letter O", "REQ_006 no area",
    "test_REQ_CMD006_no_sep", "Req-Com-001 mixed case",
    "REQ_HMI_006 ok then REQ_HMI_7 bad", "testREQ_HMI_06 camelCase",
    "test_REQ_CMD_002_retry_req_cmd_3_times",  # 영역 + 숫자가 붙은 약어는 ID와 구분할 수 없다 (STP 2.7: 쓰지 않음)
])
def test_malformed_req_ids_are_flagged(name):
    assert c.malformed_req(name, AREAS)


@pytest.mark.parametrize("name", [
    "REQ_HMI_006 resync after req_timeout_3 retries", "freq_hz_10 sampling", "test_prereq_step_1",
    "prereq step 1", "test_requires_nothing_1s", "Suite.REQ_COM_003_lost_after_1s", "test_REQ_CMD_006_acks",
    "REQ-HMI-001 hyphen", "plain test without id",
    "WsServer.REQ_COM_008_rejects_HTTP_REQUEST_body", "test_REQ_CMD_006_REQUIRED_fields",
    "REQ_HMI_006 a resync REQUEST is sent once", "REQUIRES nothing", "PREREQ_1", "testREQ_HMI_006 camelCase ok",
    "test_REQ_CMD_002_MAX_REQ_SIZE", "WsServer.REQ_COM_008_rejects_oversized_REQ_header",
    "REQ_HMI_007 rejects oversized REQ header", "REQ_PERF_001 handles 100 REQ/s",
])
def test_well_formed_or_unrelated_names_pass(name):
    assert not c.malformed_req(name, AREAS)


def test_empty_area_list_is_a_tool_error():
    with pytest.raises(c.ToolError):
        c.malformed_req("REQ_HMI_006", ())


def test_req_id_reads_only_exact_ids():
    assert c.req_id("REQ_HMI_006 x") == "REQ-HMI-006"
    assert c.req_id("REQ_HMI_0067 x") is None
    assert c.req_id("no id") is None


STP_SAMPLE = """
| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-A-001-01 | U | a | b | `Suite.REQ_HMI_001_x`, `test_REQ_HMI_001_y`, `a.test.ts > REQ_HMI_001 z` |
| TC-A-001-02 | D | a `x | y` b | c | `hmi/e2e/smoke.mjs` 2단계 (`--clock-skew 3000`) |
| TC-A-001-03 | U | a | b | `gwConnection.test.tsx > REQ_HMI_006 NO` |
| TC-A-001-04 | U | a | b | `hmiController.test.ts> REQ_HMI_006 NO` |
| TC-A-001-05 | U | a | b | 없음 |

| 항목 | 대응 TC |
|---|---|
| 헤더 | `Codec.REQ_QUAL_001_v`, `Frame.REQ_QUAL_001_*` |

| TC | 내용 | 방법 |
|---|---|---|
| TC-B-001-01 | `Ghost.REQ_COM_001_in_content` | `a.test.ts > REQ_HMI_001 z` |

| TC ID | 수준 | 테스트 |
|---|---|---|
| TC-C-001-01 | U | `Ghost.REQ_COM_001_row_before_separator` |
|---|---|---|
  | TC-C-001-02 | U | `Ghost.REQ_COM_001_indented_row` |
| TC-C-001-03 | U | `--nothing` |

- **자동화**: `tests/x.py::test_REQ_COM_001_outside_table` (`-m slow`)
"""


def test_stp_table_is_parsed_by_header_column():
    r = c.StpRefs(STP_SAMPLE)
    # 시험 이름은 테스트 칸이 아닌 칸·다른 표에 있어도 모은다 (코드에 없으면 불일치로 잡히도록)
    assert r.cpp == {
        "Suite.REQ_HMI_001_x", "Codec.REQ_QUAL_001_v", "Ghost.REQ_COM_001_in_content",
        "Ghost.REQ_COM_001_row_before_separator",  # 다음 줄이 구분선이어도 TC 행은 머리 행이 아니다
        "Ghost.REQ_COM_001_indented_row",  # 앞 공백이 있는 표 행
    }
    assert r.py == {"test_REQ_HMI_001_y", "test_REQ_COM_001_outside_table"}  # 표 밖 줄
    assert r.ts == {"a.test.ts > REQ_HMI_001 z"}
    assert r.unparsed == [
        "TC-A-001-03: 테스트 칸에 시험 이름이나 시연 점검 참조가 없음",
        "TC-A-001-03: gwConnection.test.tsx > REQ_HMI_006 NO",
        "TC-A-001-04: 테스트 칸에 시험 이름이나 시연 점검 참조가 없음",
        "TC-A-001-04: hmiController.test.ts> REQ_HMI_006 NO",
        "TC-A-001-05: 테스트 칸에 시험 이름이나 시연 점검 참조가 없음",
        'TC-B-001-01: 표에 "테스트" 열이 없음',
        "TC-C-001-03: 테스트 칸에 시험 이름이나 시연 점검 참조가 없음",
    ]
