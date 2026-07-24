# E1 Mixed-Load Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record baseline HTTP contention numbers (search-only vs write-only vs mixed) for Phase E1, with no core/API lock changes.

**Architecture:** Untracked bash probe reuses the macro Release bootstrap (`hound` + synthetic corpus seed 42). `hey` measures `/search` latency; a Python writer loops `POST /index` with rotating `doc-{i % DOCS}` IDs. Three sequential scenarios on one server process; results land in `benchmarks/results/e1_mixed_<stamp>.txt` and a Phase 2 changelog entry in `docs/REFINEMENT.md`.

**Tech Stack:** bash, Python 3 stdlib (`urllib`), `hey`, CMake Release `hound`, existing `benchmarks/macro/run_macro.sh` patterns.

**Spec:** `docs/superpowers/specs/2026-07-23-e1-mixed-load-probe-design.md`

## Global Constraints

- No changes to `FuzzyIndex`, `HttpApi`, ranking, or public HTTP JSON.
- Probe scripts under `scripts/tmp/` stay **untracked** (never `git add` them).
- Add `scripts/tmp/` to `.gitignore` so probes cannot be committed by accident.
- Start `hound` **without** `--snapshot` (upsert-only write path).
- Default hey flags: `-disable-keepalive` (same as macro).
- Do not run micro/`compare_bench`/`save_baseline` for this slice.
- Docs/changelog in English.
- Conventional commits when committing tracked files.

## File structure

| Path | Role | Tracked? |
|------|------|----------|
| `.gitignore` | Ignore `scripts/tmp/` | yes |
| `scripts/tmp/e1_writer.py` | Continuous `POST /index` rotator; prints writes/s | no |
| `scripts/tmp/probe_e1_mixed_load.sh` | Bootstrap + 3 scenarios + artifact | no |
| `benchmarks/results/e1_mixed_*.txt` | hey + writer output | already gitignored |
| `docs/REFINEMENT.md` | Status + Phase 2 changelog with numbers | yes |

---

### Task 1: Ignore `scripts/tmp/` permanently

**Files:**
- Modify: `.gitignore`
- Tracked commit only (no probe files)

**Interfaces:**
- Consumes: none
- Produces: git ignores everything under `scripts/tmp/`

- [ ] **Step 1: Append ignore rule**

Add this line to `.gitignore` (after the existing `benchmarks/results/*` block is fine):

```gitignore
# Local untracked probes (Phase E1, issue #1 scratch, etc.)
/scripts/tmp/
```

- [ ] **Step 2: Verify ignore works**

Run:

```bash
git check-ignore -v scripts/tmp/probe_e1_mixed_load.sh || true
git status --short scripts/tmp/
```

Expected: `check-ignore` prints a rule matching `scripts/tmp/`; `git status` does **not** list `scripts/tmp/` as untracked.

- [ ] **Step 3: Commit**

```bash
git add .gitignore
git commit -m "$(cat <<'EOF'
chore: gitignore scripts/tmp probe scratch

EOF
)"
```

---

### Task 2: Writer helper (`e1_writer.py`)

**Files:**
- Create: `scripts/tmp/e1_writer.py` (untracked)

**Interfaces:**
- Consumes: CLI args `--host`, `--port`, `--docs`, `--duration` (seconds; `0` = run until SIGTERM), optional `--workers` (default `1`)
- Produces: stdout final line `writes=<n> duration_s=<f> writes_per_s=<f>`; exit 0 on clean stop; non-zero if zero successful writes when duration > 0

- [ ] **Step 1: Write the writer script**

Create `scripts/tmp/e1_writer.py` with this content:

```python
#!/usr/bin/env python3
"""E1 continuous upsert writer — untracked probe helper. Do not commit."""
from __future__ import annotations

import argparse
import json
import signal
import sys
import threading
import time
import urllib.error
import urllib.request

STOP = threading.Event()


def handle_signal(signum, frame):  # noqa: ARG001
    STOP.set()


def worker(host: str, port: int, docs: int, counter: list[int], errors: list[int],
           lock: threading.Lock, start_id: int) -> None:
    i = start_id
    url = f"http://{host}:{port}/index"
    while not STOP.is_set():
        doc_id = f"doc-{i % docs}"
        body = json.dumps(
            {
                "id": doc_id,
                "text": f"Writer Upsert {i}",
                "external_score": float(i % 100),
            }
        ).encode()
        req = urllib.request.Request(
            url, data=body, method="POST", headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                resp.read()
            with lock:
                counter[0] += 1
        except (urllib.error.URLError, TimeoutError, OSError):
            if STOP.is_set():
                break
            with lock:
                errors[0] += 1
        i += 1


def main() -> int:
    p = argparse.ArgumentParser(description="E1 POST /index rotator")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, required=True)
    p.add_argument("--docs", type=int, required=True)
    p.add_argument("--duration", type=float, default=0.0,
                   help="Seconds to run; 0 = until SIGTERM/SIGINT")
    p.add_argument("--workers", type=int, default=1)
    args = p.parse_args()

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    counter = [0]
    errors = [0]
    lock = threading.Lock()
    t0 = time.perf_counter()
    threads = [
        threading.Thread(
            target=worker,
            args=(args.host, args.port, args.docs, counter, errors, lock, w),
            daemon=True,
        )
        for w in range(args.workers)
    ]
    for t in threads:
        t.start()

    if args.duration > 0:
        STOP.wait(timeout=args.duration)
        STOP.set()
    else:
        while not STOP.is_set():
            time.sleep(0.05)

    for t in threads:
        t.join(timeout=2.0)

    elapsed = max(time.perf_counter() - t0, 1e-9)
    writes = counter[0]
    rate = writes / elapsed
    print(
        f"writes={writes} errors={errors[0]} duration_s={elapsed:.3f} writes_per_s={rate:.1f}",
        flush=True,
    )
    if args.duration > 0 and writes == 0:
        print("ERROR: zero successful writes", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Smoke-check syntax**

Run:

```bash
python3 -m py_compile scripts/tmp/e1_writer.py
chmod +x scripts/tmp/e1_writer.py
```

Expected: exit 0; no output from `py_compile`.

- [ ] **Step 3: Do not commit this file**

Confirm:

```bash
git status --short scripts/tmp/e1_writer.py
```

Expected: empty (ignored) or at most ignored path — **do not** `git add` it.

---

### Task 3: Probe driver (`probe_e1_mixed_load.sh`)

**Files:**
- Create: `scripts/tmp/probe_e1_mixed_load.sh` (untracked)
- Depends on: `scripts/tmp/e1_writer.py` from Task 2

**Interfaces:**
- Consumes: env `HOUND_MACRO_N` (default 2000), `HOUND_MACRO_C` (50), `HOUND_MACRO_DOCS` (5000), `HOUND_E1_DURATION` (15), `HOUND_HEY_EXTRA` (`-disable-keepalive`), `HOUND_MACRO_PORT`, `HOUND_BENCH_BUILD_DIR`, `HOUND_BENCH_OUT_DIR`
- Produces: `benchmarks/results/e1_mixed_<UTC>.txt` with sections `search-only`, `write-only`, `mixed` and a `SUMMARY` block extracting Latency distribution lines

- [ ] **Step 1: Write the probe script**

Create `scripts/tmp/probe_e1_mixed_load.sh`:

```bash
#!/usr/bin/env bash
# E1 mixed-load concurrency probe — untracked. Do not commit.
# Spec: docs/superpowers/specs/2026-07-23-e1-mixed-load-probe-design.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${HOUND_BENCH_BUILD_DIR:-$ROOT/build-bench}"
OUT_DIR="${HOUND_BENCH_OUT_DIR:-$ROOT/benchmarks/results}"
WRITER="$ROOT/scripts/tmp/e1_writer.py"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_TXT="${1:-$OUT_DIR/e1_mixed_${STAMP}.txt}"

N="${HOUND_MACRO_N:-2000}"
C="${HOUND_MACRO_C:-50}"
DOCS="${HOUND_MACRO_DOCS:-5000}"
DURATION="${HOUND_E1_DURATION:-15}"
HOST="127.0.0.1"
# shellcheck disable=SC2206
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

if [[ ! -f "$WRITER" ]]; then
  echo "ERROR: missing $WRITER (Task 2)"
  exit 1
fi
if ! HEY_BIN="$(find_hey)"; then
  echo "ERROR: hey not found. Install: go install github.com/rakyll/hey@latest"
  exit 1
fi

mkdir -p "$OUT_DIR"
DATA_DIR="$(mktemp -d "${TMPDIR:-/tmp}/hound-e1.XXXXXX")"
CORPUS="$DATA_DIR/corpus.csv"
WRITER_PID=""
HOUND_PID=""

cleanup() {
  if [[ -n "${WRITER_PID}" ]] && kill -0 "$WRITER_PID" 2>/dev/null; then
    kill "$WRITER_PID" 2>/dev/null || true
    wait "$WRITER_PID" 2>/dev/null || true
  fi
  if [[ -n "${HOUND_PID}" ]] && kill -0 "$HOUND_PID" 2>/dev/null; then
    kill "$HOUND_PID" 2>/dev/null || true
    wait "$HOUND_PID" 2>/dev/null || true
  fi
  rm -rf "$DATA_DIR"
}
trap cleanup EXIT

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

# No --snapshot: upsert-only write path (spec risk mitigation).
"$BUILD/hound" --host "$HOST" --port "$PORT" --load "$CORPUS" \
  >"$DATA_DIR/server.log" 2>&1 &
HOUND_PID=$!

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

DOC_TEXT="$(python3 - "$CORPUS" <<'PY'
import csv, sys
with open(sys.argv[1], newline="") as f:
    rows = list(csv.DictReader(f))
print(rows[0]["text"])
PY
)"
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

run_search_hey() {
  local label="$1"
  echo "========== ${label} /search exact =========="
  echo "q=$DOC_TEXT"
  "$HEY_BIN" "${HEY_EXTRA[@]}" -n "$N" -c "$C" -t 10 \
    "http://${HOST}:${PORT}/search?q=${EXACT_Q_ENC}&limit=10"
  echo
  echo "========== ${label} /search typo =========="
  echo "q=$TYPO_Q"
  "$HEY_BIN" "${HEY_EXTRA[@]}" -n "$N" -c "$C" -t 10 \
    "http://${HOST}:${PORT}/search?q=${TYPO_Q_ENC}&limit=10"
  echo
}

{
  echo "hound E1 mixed-load probe"
  echo "date_utc=$STAMP"
  echo "hey=$HEY_BIN"
  echo "host=$HOST port=$PORT docs=$DOCS n=$N c=$C duration_s=$DURATION"
  echo "NOTE: no --snapshot; includes loopback + HTTP + JSON (not micro µs)"
  echo

  echo "########## SCENARIO: search-only ##########"
  run_search_hey "search-only"

  echo "########## SCENARIO: write-only ##########"
  echo "writer duration_s=$DURATION docs=$DOCS"
  python3 "$WRITER" --host "$HOST" --port "$PORT" --docs "$DOCS" \
    --duration "$DURATION" --workers 1
  echo

  echo "########## SCENARIO: mixed ##########"
  MIX_LOG="$DATA_DIR/writer_mixed.log"
  python3 "$WRITER" --host "$HOST" --port "$PORT" --docs "$DOCS" \
    --duration 0 --workers 1 >"$MIX_LOG" 2>&1 &
  WRITER_PID=$!
  sleep 0.2
  run_search_hey "mixed"
  kill "$WRITER_PID" 2>/dev/null || true
  wait "$WRITER_PID" 2>/dev/null || true
  WRITER_PID=""
  echo "========== mixed writer =========="
  cat "$MIX_LOG"
  echo

  echo "########## SUMMARY (paste into REFINEMENT) ##########"
  echo "Extract Latency Distribution 50%/95%/99% from sections above."
  echo "Compare search-only vs mixed (exact and typo)."
  echo "Write-only and mixed writer lines: writes=… writes_per_s=…"
} | tee "$OUT_TXT"

echo "wrote $OUT_TXT"
```

- [ ] **Step 2: Make executable and bash -n**

```bash
chmod +x scripts/tmp/probe_e1_mixed_load.sh
bash -n scripts/tmp/probe_e1_mixed_load.sh
```

Expected: exit 0.

- [ ] **Step 3: Do not commit the probe**

```bash
git check-ignore -v scripts/tmp/probe_e1_mixed_load.sh
```

Expected: matched by `/scripts/tmp/` rule from Task 1.

---

### Task 4: Run the probe and capture numbers

**Files:**
- Create (runtime): `benchmarks/results/e1_mixed_<stamp>.txt` (gitignored)

**Interfaces:**
- Consumes: scripts from Tasks 2–3; Release toolchain; `hey` on PATH
- Produces: filled artifact with three scenarios; operator extracts p50/p95/p99 + writes/s

- [ ] **Step 1: Run full probe**

```bash
./scripts/tmp/probe_e1_mixed_load.sh
```

Expected: ends with `wrote …/benchmarks/results/e1_mixed_YYYYMMDDTHHMMSSZ.txt`; hound stays up through all three scenarios; no early exit.

Optional faster smoke (not for changelog numbers):

```bash
HOUND_MACRO_N=200 HOUND_MACRO_C=20 HOUND_MACRO_DOCS=1000 HOUND_E1_DURATION=5 \
  ./scripts/tmp/probe_e1_mixed_load.sh benchmarks/results/e1_mixed_smoke.txt
```

- [ ] **Step 2: Extract metrics into a table**

From the artifact, fill:

| scenario | query | p50 | p95 | p99 | writes/s |
|----------|-------|-----|-----|-----|----------|
| search-only | exact | | | | — |
| search-only | typo | | | | — |
| write-only | — | — | — | — | |
| mixed | exact | | | | (from writer line) |
| mixed | typo | | | | same writer |

hey prints lines like:

```text
  50% in X.X secs
  95% in X.X secs
  99% in X.X secs
```

Convert to ms for the changelog if values are small (`secs * 1000`).

- [ ] **Step 3: Sanity checks before documenting**

- Mixed search p99 ≥ search-only p99 on at least one of exact/typo **or** changelog notes “no measurable contention at this load” (still a valid E1 ship — numbers recorded).
- write-only `writes_per_s` > 0.
- If mixed `writes_per_s` ≈ 0, treat as probe failure and fix writer/port before changelog.

No git commit in this task (artifact is gitignored).

---

### Task 5: Update `docs/REFINEMENT.md` status + changelog

**Files:**
- Modify: `docs/REFINEMENT.md` (status block at top + Phase E table + Phase 2 changelog + Future decisions)

**Interfaces:**
- Consumes: metrics table from Task 4; artifact path
- Produces: E1 marked done; next = E2; changelog entry with real numbers

- [ ] **Step 1: Update status banner**

Replace the “Paused after Phase D + issue #1” / “Next planned slice: **Phase E1**” wording so that:

- Phases A–D + #1 + **E1** complete
- Next planned slice: **Phase E2** (double-buffer / publish-swap)
- Add E1 row under Done (shipped), e.g. probe recorded search p99 delta (cite numbers)
- Move E out of “Not started” / mark E1 done in the Phase E table (`E1` Status **Done**)

- [ ] **Step 2: Insert Phase 2 changelog entry at the top of the changelog section**

Use real numbers from Task 4 (replace placeholders):

```markdown
### 2026-07-23 — Phase E1 mixed-load concurrency probe

```text
Hypothesis: Mixed continuous upserts raise /search p99 vs search-only via
            unique_lock + HttpApi write mutex.
Primary metric(s):   hey /search p50/p95/p99 (exact + typo), search-only vs mixed
Secondary metric(s): writes/s write-only and mixed
Before: search-only (+ write-only) in benchmarks/results/e1_mixed_<STAMP>.txt
After:  mixed in same artifact
Correctness: N/A (probe only; no index/API change)
Micro gate:  N/A
DoD items:   [x] three scenarios  [x] numbers recorded  [x] scripts/tmp gitignored
Decision:    ship — E1 baseline recorded; next E2
```

- Commands: `./scripts/tmp/probe_e1_mixed_load.sh` (untracked; local only)
- Metrics:

  | scenario | query | p50 | p95 | p99 | writes/s |
  |----------|-------|-----|-----|-----|----------|
  | search-only | exact | … | … | … | — |
  | search-only | typo | … | … | … | — |
  | write-only | — | — | — | — | … |
  | mixed | exact | … | … | … | … |
  | mixed | typo | … | … | … | … |

- Contention signal: mixed − search-only p99 (exact / typo) = …
- Correctness: N/A
- Micro gate: N/A
- Decision: **ship** — unlocks E2 design spike
- Notes: no `--snapshot`; hey `-disable-keepalive`; probe stays under `scripts/tmp/`
```

Also update “Future decisions” item 1 to say E2 is next (E1 done).

- [ ] **Step 3: Commit docs only**

```bash
git add docs/REFINEMENT.md
git status
git commit -m "$(cat <<'EOF'
docs: record Phase E1 mixed-load probe baseline

EOF
)"
```

Confirm `scripts/tmp/` is still untracked/ignored and not in the commit.

---

## Self-review (plan vs spec)

| Spec requirement | Task |
|------------------|------|
| Untracked `probe_e1_mixed_load.sh` | Task 3 |
| Python writer, rotating IDs, writes/s | Task 2 |
| Three scenarios: search / write / mixed | Task 3–4 |
| Artifact `e1_mixed_<stamp>.txt` | Task 3–4 |
| No `--snapshot` | Task 3 |
| Env defaults N/C/DOCS/DURATION/HEY_EXTRA | Task 3 |
| `scripts/tmp/` gitignored | Task 1 |
| REFINEMENT changelog + status → E2 next | Task 5 |
| No FuzzyIndex/HttpApi/micro/baseline | Global + all tasks |

No placeholders left in script bodies. Writer `--duration 0` + SIGTERM matches mixed stop semantics in the spec.
