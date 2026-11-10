#!/usr/bin/env python3
"""cppcheck용 compile_commands.json: 외부 의존성(_deps)의 include 경로를 뺀 사본을 만든다.

cppcheck는 외부 헤더(nlohmann/json 등)에서 syntaxError를 내면 그 헤더를 include한 번역 단위 전체의
분석을 중단한다. 외부 헤더를 아예 읽지 않게 하면(missingIncludeSystem만 남음) 우리 코드는 모두 검사된다.

사용: cppcheck_compile_commands.py <in.json> <out.json>
"""
import json
import shlex
import sys


def strip_deps(args: list[str]) -> list[str]:
    out: list[str] = []
    skip_next = False
    for i, a in enumerate(args):
        if skip_next:
            skip_next = False
            continue
        if a in ("-I", "-isystem") and i + 1 < len(args) and "/_deps/" in args[i + 1]:
            skip_next = True
            continue
        if a.startswith("-I") and "/_deps/" in a:
            continue
        out.append(a)
    return out


def main() -> None:
    src, dst = sys.argv[1], sys.argv[2]
    entries = json.load(open(src, encoding="utf-8"))
    for e in entries:
        # 문자열로 다시 합치면 -DNAME=\"...\" 인용이 깨지므로 arguments 목록으로 넘긴다
        e["arguments"] = strip_deps(shlex.split(e.pop("command")))
    json.dump(entries, open(dst, "w", encoding="utf-8"), indent=1)


if __name__ == "__main__":
    main()
