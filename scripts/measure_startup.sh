#!/usr/bin/env bash
# Measures app startup: time from main() to first event loop pass after the window is shown,
# plus total wall-clock time of the process. Spec target: < 1000 ms (M0 goal: < 300 ms).
#
# Usage: scripts/measure_startup.sh [build-dir] [runs]
#   build-dir defaults to build/release, runs defaults to 5.
set -euo pipefail

BUILD_DIR="${1:-build/release}"
RUNS="${2:-5}"
LIMIT_MS="${CHESSGUI_STARTUP_LIMIT_MS:-1000}"

if [[ "$(uname)" == "Darwin" ]]; then
    APP="$BUILD_DIR/app/ChessGui.app/Contents/MacOS/ChessGui"
else
    APP="$BUILD_DIR/app/ChessGui"
fi

if [[ ! -x "$APP" ]]; then
    echo "App not found at $APP. Build it first, e.g.: cmake --workflow --preset dev" >&2
    exit 1
fi

worst=0
for ((i = 1; i <= RUNS; i++)); do
    start=$(python3 -c 'import time; print(int(time.time() * 1000))')
    inner=$(CHESSGUI_STARTUP_EXIT=1 "$APP" | sed -n 's/^startup_ms=//p')
    end=$(python3 -c 'import time; print(int(time.time() * 1000))')
    wall=$((end - start))
    echo "run $i: main->first frame ${inner} ms, process wall ${wall} ms"
    ((wall > worst)) && worst=$wall
done

echo "worst wall time: ${worst} ms (limit ${LIMIT_MS} ms)"
if ((worst > LIMIT_MS)); then
    echo "FAIL: startup exceeds limit" >&2
    exit 1
fi
