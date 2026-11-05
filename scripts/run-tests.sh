#!/usr/bin/env bash
# Nota - local verification (macOS / Linux).  Windows: scripts\run-tests.bat
# Kept ASCII-only to match the batch file.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG="$PWD/nota-test-log.txt"
FAIL=0
{ echo "Nota local verification"; echo "Date: $(date)"; echo; } > "$LOG"

echo
echo "============================================================"
echo " Nota local verification - log: $LOG"
echo "============================================================"
echo

echo "[0/4] Environment"
command -v cmake >/dev/null || { echo "  [X] cmake not found"; exit 1; }
{ cmake --version; git --version; } >> "$LOG" 2>&1
echo "  $(cmake --version | head -1)"
if git rev-parse --short HEAD >/dev/null 2>&1; then
  echo "  commit $(git rev-parse --short HEAD)"
  git rev-parse --short HEAD >> "$LOG" 2>&1
fi
echo

echo "[1/4] Configure"
{ echo; echo "---- configure ----"; } >> "$LOG"
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo >> "$LOG" 2>&1 \
  || { echo "  [X] configure failed - see $LOG"; exit 1; }
echo "  ok"
echo

echo "[2/4] Build"
{ echo; echo "---- build ----"; } >> "$LOG"
cmake --build build --config RelWithDebInfo >> "$LOG" 2>&1 \
  || { echo "  [X] build failed"; grep -i error "$LOG" | head -20; exit 1; }
echo "  ok"
TESTBIN=$(find build -name 'nota_core_tests' -type f 2>/dev/null | head -1)
TOOLBIN=$(find build -name 'nota-analyze' -type f 2>/dev/null | head -1)
TOOLDIR=$([ -n "$TOOLBIN" ] && dirname "$TOOLBIN" || echo "")
echo "  binary: $TESTBIN"
WARNCOUNT=$(grep -ci "warning" "$LOG" || true)
echo "  compiler warnings: $WARNCOUNT"
echo

echo "[3/4] Unit tests"
{ echo; echo "---- unit tests ----"; } >> "$LOG"
"$TESTBIN" >> "$LOG" 2>&1 || FAIL=1
grep -E "passed, .* failed" "$LOG" | tail -1 | sed 's/^/  /'
echo

echo "[4/4] CLI"
{ echo; echo "---- CLI ----"; } >> "$LOG"
if [ -z "$TOOLDIR" ]; then
  echo "  skipped - tools not built"
else
  for t in nota-analyze nota-geometry nota-scroll; do
    { echo; echo "-- $t --help --"; } >> "$LOG"
    if "$TOOLDIR/$t" --help >> "$LOG" 2>&1; then
      echo "  $t : ok"
    else
      echo "  $t : FAILED"; FAIL=1
    fi
  done
fi
echo

echo "============================================================"
[ "$FAIL" = "0" ] && echo " RESULT: PASS" || echo " RESULT: FAIL - see the log"
echo " Log: $LOG"
echo "============================================================"
exit $FAIL
