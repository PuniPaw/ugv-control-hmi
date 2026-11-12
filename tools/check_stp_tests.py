#!/usr/bin/env python3
"""STP(docs/04_STP.md)의 "테스트" 열에 적힌 이름이 실제로 존재하는지 확인한다 (STP 2.7: TC와 테스트 1:1).

- C++: `Suite.REQ_...` 이름을 gtest 실행 파일의 --gtest_list_tests 출력과 비교
- Python: `test_REQ_...` 이름을 `pytest --collect-only`가 python/ sim/ tests/ 에서 모은 실제 시험과 비교
  (클래스 메서드·async 시험도 포함, 매개변수 `[...]`는 뗀다)
- TypeScript(HMI): `<파일>.test.ts > <시험 이름>`을 `vitest list --json`이 낸 실제 시험 목록과 비교
  (정규식으로 it("...")를 찾지 않는다: 따옴표 종류, test(), it.each 등을 놓치지 않도록. hmi/에서 npm install 필요)

RTM(06)을 grep으로 만들 때 이름이 어긋나면 그 TC가 "시험 없음"으로 보이므로, 어긋나면 실패한다.

반대 방향도 검사한다: 코드의 시험 이름에 붙은 REQ ID가 SRS(docs/01_SRS.md)에 없으면 실패한다.
RTM은 이름의 REQ ID로 요구사항을 잇기 때문에, 없는 ID(오타)는 추적표에서 조용히 사라진다.
(ID가 있지만 시험 내용과 맞지 않는 경우는 기계로 못 잡는다. 리뷰에서 본다.)

사용: tools/check_stp_tests.py <ugvc_tests 실행 파일>
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STP = Path(os.environ.get("UGVC_STP", ROOT / "docs" / "04_STP.md"))
SRS = ROOT / "docs" / "01_SRS.md"


def stp_names() -> tuple[set[str], set[str], set[str]]:
    text = STP.read_text(encoding="utf-8")
    cpp = set(re.findall(r"`([A-Za-z0-9]+\.REQ_[A-Za-z0-9_]+)`", text))
    py = set(re.findall(r"`(test_REQ_[A-Za-z0-9_]+)`", text))
    py |= set(re.findall(r"::(test_REQ_[A-Za-z0-9_]+)`", text))
    ts = set(re.findall(r"`([^`\s]+\.test\.ts > [^`]+)`", text))
    return cpp, py, ts


class ToolError(Exception):
    """시험 목록을 받지 못함 (STP 불일치와 구분: 종료 코드 2)"""


def run_list(cmd: list[str], cwd: Path | None, hint: str, env: dict[str, str] | None = None) -> str:
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, env=env)
    except OSError as e:  # 없음, 디렉터리, 실행 권한 없음 등
        raise ToolError(f"{' '.join(cmd)}: {e}. {hint}") from e
    if r.returncode != 0:
        raise ToolError(f"{' '.join(cmd)} 실패 (종료 코드 {r.returncode}). {hint}\n{r.stderr.strip()}")
    return r.stdout


def gtest_names(binary: str) -> set[str]:
    hint = "gateway를 빌드했는지 확인 (cmake --build gateway/build)"
    out = run_list([binary, "--gtest_list_tests"], None, hint)
    names, suite = set(), ""
    for line in out.splitlines():
        if not line.startswith(" "):
            suite = line.strip().rstrip(".")
        else:
            names.add(f"{suite}.{line.split()[0]}")
    if not names:
        raise ToolError(f"{binary}: gtest 시험 목록이 비어 있음 (gtest 실행 파일이 맞는지 확인). {hint}")
    return names


def pytest_names() -> set[str]:
    """pytest가 수집한 시험 함수 이름 (nodeid의 마지막 부분, 매개변수 [...] 제외)"""
    env = {**os.environ, "PYTHONPATH": os.pathsep.join([str(ROOT / "python"), str(ROOT / "sim")])}
    out = run_list([sys.executable, "-m", "pytest", "--collect-only", "-q", "-p", "no:cacheprovider", "python", "sim", "tests"],
                   ROOT, "pytest 설치 확인 (pip install pytest websockets)", env)
    names = {line.split("::")[-1].split("[")[0] for line in out.splitlines() if "::" in line}
    if not names:
        raise ToolError("pytest가 시험을 하나도 수집하지 못함")
    return names


def vitest_names() -> set[str]:
    """vitest가 수집한 시험 → "<파일 이름> > <시험 이름>" (describe가 있으면 "a > b"처럼 이름에 들어간다).
    STP는 파일 이름만 쓰므로, 다른 폴더에 같은 이름의 시험 파일이 생기면 구분할 수 없어 실패시킨다."""
    hint = "hmi/에서 npm install이 필요할 수 있음"
    out = run_list(["npx", "vitest", "list", "--json"], ROOT / "hmi", hint)
    try:
        tests = json.loads(out)
        if not (isinstance(tests, list) and tests and all(isinstance(t, dict) and isinstance(t.get("file"), str)
                                                          and isinstance(t.get("name"), str) for t in tests)):
            raise ValueError("시험 목록 형식이 아님")
    except ValueError as e:  # JSONDecodeError 포함
        raise ToolError(f"vitest list --json 출력을 해석하지 못함: {e}. {hint}\n{out[:300]}") from e
    by_name: dict[str, set[str]] = {}
    for t in tests:
        by_name.setdefault(Path(t["file"]).name, set()).add(t["file"])
    dup = {n: f for n, f in by_name.items() if len(f) > 1}
    if dup:
        raise ToolError(f"이름이 같은 vitest 시험 파일이 여러 폴더에 있음: {dup}")
    return {f"{Path(t['file']).name} > {t['name']}" for t in tests}


def srs_ids() -> set[str]:
    return set(re.findall(r"^\| (REQ-[A-Z]+-\d{3}) \|", SRS.read_text(encoding="utf-8"), re.M))


def req_id(test_name: str) -> str | None:
    m = re.search(r"REQ[_-]([A-Z]+)[_-](\d{3})", test_name)
    return f"REQ-{m.group(1)}-{m.group(2)}" if m else None


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    cpp, py, ts = stp_names()
    try:
        code = gtest_names(sys.argv[1]) | pytest_names() | vitest_names()
    except ToolError as e:
        print(f"시험 목록을 받지 못함: {e}", file=sys.stderr)
        return 2
    missing = sorted((cpp | py | ts) - code)
    for name in missing:
        print(f"STP에 있지만 코드에 없는 테스트: {name}")
    known = srs_ids()
    unknown = sorted(n for n in code if (rid := req_id(n)) and rid not in known)
    for name in unknown:
        print(f"SRS에 없는 REQ ID를 단 테스트: {name}")
    print(f"STP 테스트 이름 {len(cpp) + len(py) + len(ts)}개 확인, 불일치 {len(missing)}개. "
          f"코드의 REQ 시험 {sum(1 for n in code if req_id(n))}개 중 SRS에 없는 ID {len(unknown)}개")
    return 1 if missing or unknown else 0


if __name__ == "__main__":
    sys.exit(main())
