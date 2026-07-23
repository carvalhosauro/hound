#!/usr/bin/env bash
# Run perf stat against hound_bench_micro (Release, no sanitizers).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${HOUND_BENCH_BUILD_DIR:-$ROOT/build-bench}"
BIN="$BUILD/hound_bench_micro"

if ! command -v perf >/dev/null 2>&1; then
  echo "ERROR: perf not found. Install linux-perf / perf package, then retry."
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DHOUND_BUILD_BENCH=ON -DHOUND_ENABLE_TSAN=OFF -DHOUND_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)" --target hound_bench_micro

EVENTS="${HOUND_PERF_EVENTS:-cycles,instructions,cache-references,cache-misses,branches,branch-misses}"

echo "perf stat -e $EVENTS -- $BIN $*"
perf stat -e "$EVENTS" -- "$BIN" "$@"
