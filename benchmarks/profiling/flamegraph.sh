#!/usr/bin/env bash
# Record a CPU profile of hound_bench_micro and emit a FlameGraph SVG.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${HOUND_BENCH_BUILD_DIR:-$ROOT/build-bench}"
BIN="$BUILD/hound_bench_micro"
OUT_DIR="${HOUND_BENCH_OUT_DIR:-$ROOT/benchmarks/results}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
DATA="$OUT_DIR/perf_${STAMP}.data"
FOLDED="$OUT_DIR/perf_${STAMP}.folded"
SVG="$OUT_DIR/perf_${STAMP}.svg"

FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-$HOME/src/FlameGraph}"

if ! command -v perf >/dev/null 2>&1; then
  echo "ERROR: perf not found. Install linux-perf / perf package, then retry."
  exit 1
fi

if [[ ! -x "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" || ! -x "$FLAMEGRAPH_DIR/flamegraph.pl" ]]; then
  echo "ERROR: FlameGraph scripts not found under FLAMEGRAPH_DIR=$FLAMEGRAPH_DIR"
  echo "Clone: git clone https://github.com/brendangregg/FlameGraph.git \"\$FLAMEGRAPH_DIR\""
  exit 1
fi

mkdir -p "$OUT_DIR"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DHOUND_BUILD_BENCH=ON -DHOUND_ENABLE_TSAN=OFF -DHOUND_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)" --target hound_bench_micro

# Shorter runs keep flamegraphs readable; override via args.
ARGS=("$@")
if [[ ${#ARGS[@]} -eq 0 ]]; then
  ARGS=(--benchmark_filter=BM_SearchFuzzy/20000/2 --benchmark_min_time=0.5)
fi

echo "perf record -g -o $DATA -- $BIN ${ARGS[*]}"
perf record -g -o "$DATA" -- "$BIN" "${ARGS[@]}"

perf script -i "$DATA" | "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" >"$FOLDED"
"$FLAMEGRAPH_DIR/flamegraph.pl" "$FOLDED" >"$SVG"

echo "wrote $SVG"
echo "raw: $DATA  folded: $FOLDED"
