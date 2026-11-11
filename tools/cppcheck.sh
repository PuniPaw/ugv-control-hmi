#!/usr/bin/env bash
# GW 소스 정적 분석 (REQ-QUAL-002: 경고 0건, 예외는 사유를 문서화)
#
# 사용 (저장소 루트에서, gateway를 한 번 configure한 뒤):
#   tools/cppcheck.sh
# compile_commands.json으로 실제 빌드와 같은 include·define을 쓰되, 외부 의존성(_deps)의 include 경로는 뺀다.
# cppcheck는 외부 헤더(nlohmann/json)에서 syntaxError를 내면 그 헤더를 include한 파일 전체를 검사하지 않는다.
# 외부 헤더를 아예 읽지 않으면 우리 파일은 모두 검사된다 (tools/cppcheck_compile_commands.py).
# 예외는 gateway/cppcheck-suppressions.txt에 사유와 함께 적는다.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${UGVC_BUILD_DIR:-$ROOT/gateway/build}"

if [[ ! -f "$BUILD/compile_commands.json" ]]; then
  echo "compile_commands.json 없음: cmake -S gateway -B gateway/build 먼저 실행" >&2
  exit 2
fi

FILTERED="$BUILD/compile_commands.cppcheck.json"
python3 -I "$ROOT/tools/cppcheck_compile_commands.py" "$BUILD/compile_commands.json" "$FILTERED"

cppcheck \
  --project="$FILTERED" \
  --file-filter="$ROOT/gateway/src/*" \
  --file-filter="$ROOT/gateway/tests/*" \
  --enable=warning,style,performance,portability \
  --std=c++17 \
  --inline-suppr \
  --suppressions-list="$ROOT/gateway/cppcheck-suppressions.txt" \
  --error-exitcode=1 \
  --quiet \
  "$@"
echo "cppcheck: 경고 0건"
