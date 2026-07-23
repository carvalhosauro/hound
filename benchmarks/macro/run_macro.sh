#!/usr/bin/env bash
# HTTP macrobenchmark with hey against a live Hound sidecar.
# Measures latency/throughput INCLUDING loopback + HTTP + JSON overhead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${HOUND_BENCH_BUILD_DIR:-$ROOT/build-bench}"
OUT_DIR="${HOUND_BENCH_OUT_DIR:-$ROOT/benchmarks/results}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_TXT="${1:-$OUT_DIR/macro_${STAMP}.txt}"

N="${HOUND_MACRO_N:-2000}"
C="${HOUND_MACRO_C:-50}"
DOCS="${HOUND_MACRO_DOCS:-5000}"
HOST="127.0.0.1"
# hey+httplib keep-alive can inflate latency (~40ms floor); disable by default.
HEY_EXTRA=(${HOUND_HEY_EXTRA:--disable-keepalive})

find_hey() {
  if command -v hey >/dev/null 2>&1; then
    command -v hey
    return
  fi
  local gopath
  gopath="$(go env GOPATH 2>/dev/null || true)"
  if [[ -n "$gopath" && -x "$gopath/bin/hey" ]]; then
    echo "$gopath/bin/hey"
    return
  fi
  if [[ -x /home/carvalhosauro/go/bin/hey ]]; then
    echo /home/carvalhosauro/go/bin/hey
    return
  fi
  return 1
}

if ! HEY_BIN="$(find_hey)"; then
  echo "ERROR: hey not found."
  echo "Install with: GOBIN=\$(go env GOPATH)/bin go install github.com/rakyll/hey@latest"
  echo "Then ensure GOPATH/bin is on PATH."
  exit 1
fi

mkdir -p "$OUT_DIR" "$(dirname "$OUT_TXT")"
DATA_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hound-macro.XXXXXX")"
CORPUS="$DATA_DIR/corpus.csv"
cleanup() {
  if [[ -n "${HOUND_PID:-}" ]] && kill -0 "$HOUND_PID" 2>/dev/null; then
    kill "$HOUND_PID" 2>/dev/null || true
    wait "$HOUND_PID" 2>/dev/null || true
  fi
  rm -rf "$DATA_DIR"
}
trap cleanup EXIT

# Fixed-seed synthetic corpus (generic names + index), no business domain.
python3 - "$CORPUS" "$DOCS" <<'PY'
import csv, random, sys
path, n = sys.argv[1], int(sys.argv[2])
first = ["Ada","Blake","Casey","Drew","Eden","Finn","Gray","Harper","Indigo","Jules"]
last = ["Ash","Brook","Cedar","Dale","Elm","Field","Grove","Hill","Isle","Jade"]
rng = random.Random(42)
with open(path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["id", "text", "external_score"])
    for i in range(n):
        text = f"{rng.choice(first)} {rng.choice(last)} {i}"
        w.writerow([f"doc-{i}", text, round(rng.random() * 100, 3)])
print(f"wrote {n} docs → {path}")
PY

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DHOUND_BUILD_BENCH=ON -DHOUND_ENABLE_TSAN=OFF -DHOUND_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$(nproc)" --target hound

PORT="${HOUND_MACRO_PORT:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
fi

"$BUILD/hound" --host "$HOST" --port "$PORT" --load "$CORPUS" >"$DATA_DIR/server.log" 2>&1 &
HOUND_PID=$!

# Wait for /health
for _ in $(seq 1 50); do
  if curl -sf "http://${HOST}:${PORT}/health" >/dev/null; then
    break
  fi
  if ! kill -0 "$HOUND_PID" 2>/dev/null; then
    echo "hound exited early; log:"
    cat "$DATA_DIR/server.log" || true
    exit 1
  fi
  sleep 0.1
done
curl -sf "http://${HOST}:${PORT}/health" >/dev/null

# Pick a real document text for exact/typo scenarios (seed-stable first row after header).
DOC_TEXT="$(python3 - "$CORPUS" <<'PY'
import csv, sys
with open(sys.argv[1], newline="") as f:
    rows = list(csv.DictReader(f))
print(rows[0]["text"])
PY
)"
# Simple typo: drop one character from the middle token when possible.
TYPO_Q="$(python3 - "$DOC_TEXT" <<'PY'
import sys
s = sys.argv[1]
parts = s.split()
if len(parts[0]) > 2:
    parts[0] = parts[0][:-1]
print(" ".join(parts))
PY
)"
EXACT_Q_ENC="$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1]))" "$DOC_TEXT")"
TYPO_Q_ENC="$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1]))" "$TYPO_Q")"

{
  echo "hound macrobenchmark (hey)"
  echo "date_utc=$STAMP"
  echo "hey=$HEY_BIN"
  echo "host=$HOST port=$PORT docs=$DOCS n=$N c=$C"
  echo "NOTE: includes loopback + HTTP + JSON overhead (not comparable to micro µs)"
  echo
  echo "========== /health =========="
  "$HEY_BIN" "${HEY_EXTRA[@]}" -n "$N" -c "$C" -t 10 "http://${HOST}:${PORT}/health"
  echo
  echo "========== /search exact =========="
  echo "q=$DOC_TEXT"
  "$HEY_BIN" "${HEY_EXTRA[@]}" -n "$N" -c "$C" -t 10 "http://${HOST}:${PORT}/search?q=${EXACT_Q_ENC}&limit=10"
  echo
  echo "========== /search typo =========="
  echo "q=$TYPO_Q"
  "$HEY_BIN" "${HEY_EXTRA[@]}" -n "$N" -c "$C" -t 10 "http://${HOST}:${PORT}/search?q=${TYPO_Q_ENC}&limit=10"
} | tee "$OUT_TXT"

echo "wrote $OUT_TXT"
