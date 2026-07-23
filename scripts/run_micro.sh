#!/usr/bin/env bash
# Run Google Benchmark micro suite (Release, no sanitizers) → JSON artifact.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${HOUND_BENCH_BUILD_DIR:-$ROOT/build-bench}"
OUT_DIR="${HOUND_BENCH_OUT_DIR:-$ROOT/benchmarks/results}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_JSON="${1:-$OUT_DIR/micro_${STAMP}.json}"

mkdir -p "$(dirname "$OUT_JSON")"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DHOUND_BUILD_BENCH=ON -DHOUND_ENABLE_TSAN=OFF -DHOUND_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)" --target hound_bench_micro

"$BUILD/hound_bench_micro" \
  --benchmark_format=json \
  --benchmark_out="$OUT_JSON" \
  --benchmark_repetitions="${HOUND_BENCH_REPS:-1}" \
  --benchmark_display_aggregates_only=true

echo "wrote $OUT_JSON"
