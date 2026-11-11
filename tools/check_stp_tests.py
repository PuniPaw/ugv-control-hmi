#!/usr/bin/env python3
"""STP(docs/04_STP.md)의 "테스트" 열에 적힌 이름이 실제로 존재하는지 확인한다 (STP 2.7: TC와 테스트 1:1).

- C++: `Suite.REQ_...` 이름을 gtest 실행 파일의 --gtest_list_tests 출력과 비교
- Python: `test_REQ_...` 이름을 python/ sim/ tests/ 의 def test_... 와 비교

RTM(06)을 grep으로 만들 때 이름이 어긋나면 그 TC가 "시험 없음"으로 보이므로, 어긋나면 실패한다.

반대 방향도 검사한다: 코드의 시험 이름에 붙은 REQ ID가 SRS(docs/01_SRS.md)에 없으면 실패한다.
RTM은 이름의 REQ ID로 요구사항을 잇기 때문에, 없는 ID(오타)는 추적표에서 조용히 사라진다.
(ID가 있지만 시험 내용과 맞지 않는 경우는 기계로 못 잡는다. 리뷰에서 본다.)

사용: tools/check_stp_tests.py <ugvc_tests 실행 파일>
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STP = Path(os.environ.get("UGVC_STP", ROOT / "docs" / "04_STP.md"))
SRS = ROOT / "docs" / "01_SRS.md"


def stp_names() -> tuple[set[str], set[str]]:
    text = STP.read_text(encoding="utf-8")
    cpp = set(re.findall(r"`([A-Za-z0-9]+\.REQ_[A-Za-z0-9_]+)`", text))
    py = set(re.findall(r"`(test_REQ_[A-Za-z0-9_]+)`", text))
    py |= set(re.findall(r"::(test_REQ_[A-Za-z0-9_]+)`", text))
    return cpp, py


def gtest_names(binary: str) -> set[str]:
    out = subprocess.run([binary, "--gtest_list_tests"], check=True, capture_output=True, text=True).stdout
    names, suite = set(), ""
    for line in out.splitlines():
        if not line.startswith(" "):
            suite = line.strip().rstrip(".")
        else:
            names.add(f"{suite}.{line.split()[0]}")
    return names


def pytest_names() -> set[str]:
    names = set()
    for d in ("python", "sim", "tests"):
        for f in (ROOT / d).rglob("test_*.py"):
            names |= set(re.findall(r"^def (test_\w+)\(", f.read_text(encoding="utf-8"), re.M))
    return names


def srs_ids() -> set[str]:
    return set(re.findall(r"^\| (REQ-[A-Z]+-\d{3}) \|", SRS.read_text(encoding="utf-8"), re.M))


def req_id(test_name: str) -> str | None:
    m = re.search(r"REQ_([A-Z]+)_(\d{3})", test_name)
    return f"REQ-{m.group(1)}-{m.group(2)}" if m else None


def main() -> int:
    cpp, py = stp_names()
    code = gtest_names(sys.argv[1]) | pytest_names()
    missing = sorted((cpp | py) - code)
    for name in missing:
        print(f"STP에 있지만 코드에 없는 테스트: {name}")
    known = srs_ids()
    unknown = sorted(n for n in code if (rid := req_id(n)) and rid not in known)
    for name in unknown:
        print(f"SRS에 없는 REQ ID를 단 테스트: {name}")
    print(f"STP 테스트 이름 {len(cpp) + len(py)}개 확인, 불일치 {len(missing)}개. "
          f"코드의 REQ 시험 {sum(1 for n in code if req_id(n))}개 중 SRS에 없는 ID {len(unknown)}개")
    return 1 if missing or unknown else 0


if __name__ == "__main__":
    sys.exit(main())
