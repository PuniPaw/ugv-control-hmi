#!/usr/bin/env python3
"""STP(docs/04_STP.md)의 "테스트" 열에 적힌 이름이 실제로 존재하는지 확인한다 (STP 2.7: TC와 테스트 1:1).

- C++: `Suite.REQ_...` 이름을 gtest 실행 파일의 --gtest_list_tests 출력과 비교
- Python: `test_REQ_...` 이름을 python/ sim/ tests/ 의 def test_... 와 비교

RTM(06)을 grep으로 만들 때 이름이 어긋나면 그 TC가 "시험 없음"으로 보이므로, 어긋나면 실패한다.

사용: tools/check_stp_tests.py <ugvc_tests 실행 파일>
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STP = Path(os.environ.get("UGVC_STP", ROOT / "docs" / "04_STP.md"))


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


def main() -> int:
    cpp, py = stp_names()
    missing = sorted((cpp - gtest_names(sys.argv[1])) | (py - pytest_names()))
    for name in missing:
        print(f"STP에 있지만 코드에 없는 테스트: {name}")
    print(f"STP 테스트 이름 {len(cpp) + len(py)}개 확인, 불일치 {len(missing)}개")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
