#!/usr/bin/env bash
# Smoke-test compare_bench.py with an artificial +20% regression fixture.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE="$ROOT/baselines/micro_baseline.json"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

python3 - "$BASE" "$TMP" <<'PY'
import json, sys
src, dst = sys.argv[1], sys.argv[2]
data = json.loads(open(src).read())
for b in data["benchmarks"]:
    if "cpu_time" in b:
        b["cpu_time"] = float(b["cpu_time"]) * 1.20
    if "real_time" in b:
        b["real_time"] = float(b["real_time"]) * 1.20
open(dst, "w").write(json.dumps(data))
PY

echo "== expect FAIL (+20% vs 10% threshold) =="
if python3 "$ROOT/scripts/compare_bench.py" "$BASE" "$TMP" --threshold 0.10; then
  echo "ERROR: expected non-zero exit"
  exit 1
fi

echo
echo "== expect OK (same file) =="
python3 "$ROOT/scripts/compare_bench.py" "$BASE" "$BASE" --threshold 0.10

echo
echo "compare_bench smoke OK"
