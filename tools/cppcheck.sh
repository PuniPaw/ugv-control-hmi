#!/usr/bin/env bash
# GW 소스 정적 분석 (REQ-QUAL-002: 경고 0건, 예외는 사유를 문서화)
#
# 사용 (저장소 루트에서, gateway를 한 번 configure한 뒤):
#   tools/cppcheck.sh
# compile_commands.json으로 실제 빌드와 같은 include·define을 쓴다.
# 예외는 gateway/cppcheck-suppressions.txt에 사유와 함께 적는다.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${UGVC_BUILD_DIR:-$ROOT/gateway/build}"

if [[ ! -f "$BUILD/compile_commands.json" ]]; then
  echo "compile_commands.json 없음: cmake -S gateway -B gateway/build 먼저 실행" >&2
  exit 2
fi

cppcheck \
  --project="$BUILD/compile_commands.json" \
  --file-filter="$ROOT/gateway/src/*" \
  --file-filter="$ROOT/gateway/tests/*" \
  --enable=warning,style,performance,portability \
  --std=c++17 \
  --inline-suppr \
  --suppressions-list="$ROOT/gateway/cppcheck-suppressions.txt" \
  --suppress="*:$BUILD/_deps/*" \
  --error-exitcode=1 \
  --quiet \
  "$@"
echo "cppcheck: 경고 0건"
