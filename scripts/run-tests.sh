#!/usr/bin/env bash
# Nota — 로컬 검증 스크립트 (macOS / Linux)
# Windows용은 scripts/run-tests.bat
set -uo pipefail
cd "$(dirname "$0")/.."

LOG="$PWD/nota-test-log.txt"
FAIL=0
{ echo "Nota 로컬 검증"; echo "시각: $(date)"; echo; } > "$LOG"

echo
echo "============================================================"
echo " Nota 로컬 검증 시작 — 로그: $LOG"
echo "============================================================"

echo "[0/4] 환경 확인"
command -v cmake >/dev/null || { echo "  [X] cmake 없음"; exit 1; }
{ cmake --version; git --version; git rev-parse --short HEAD; } >> "$LOG" 2>&1
echo "  $(cmake --version | head -1)"
git rev-parse --short HEAD >/dev/null 2>&1 && echo "  커밋 $(git rev-parse --short HEAD)"

echo "[1/4] 빌드 설정"
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >> "$LOG" 2>&1 \
  || { echo "  [X] 설정 실패 — $LOG"; exit 1; }
echo "  완료"

echo "[2/4] 빌드"
cmake --build build --config RelWithDebInfo >> "$LOG" 2>&1 \
  || { echo "  [X] 빌드 실패"; grep -i error "$LOG" | head -20; exit 1; }
echo "  완료"

TESTBIN=$(find build -name 'nota_core_tests' -type f -perm -u+x 2>/dev/null | head -1)
TOOLDIR=$(dirname "$(find build -name 'nota-analyze' -type f 2>/dev/null | head -1)" 2>/dev/null)

echo "[3/4] 단위 테스트"
{ echo; echo "---- 단위 테스트 ----"; } >> "$LOG"
"$TESTBIN" >> "$LOG" 2>&1 || FAIL=1
grep -E "passed, .* failed" "$LOG" | tail -1 | sed 's/^/  /'

echo "[4/4] CLI 확인"
{ echo; echo "---- CLI ----"; } >> "$LOG"
for t in nota-analyze nota-geometry nota-scroll; do
  if [ -x "$TOOLDIR/$t" ]; then
    "$TOOLDIR/$t" --help >> "$LOG" 2>&1
    echo "  $t : 실행됨"
  fi
done

echo
echo "============================================================"
[ "$FAIL" = "0" ] && echo " 결과: 통과" || echo " 결과: 실패 — 로그를 보라"
echo " 로그: $LOG"
echo "============================================================"
exit $FAIL
