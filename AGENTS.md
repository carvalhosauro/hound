# AGENTS.md — Hound

Guidance for humans and coding agents working in this repository.

## What this project is

Hound is a **lightweight C++ sidecar**: fuzzy autocomplete + merge with an
**external score**. It is **not** Elasticsearch/Typesense. The RDBMS remains
the source of truth; Hound indexes generic `{ id, text, external_score }` and
returns ranked ids.

- Domain-agnostic core — no business schemas or real private data
- Example/bench data is always **synthetic**
- Docs and user-facing comments: **English**
- License: MIT

Design notes: [`docs/PLANO.md`](docs/PLANO.md), post-MVP priorities:
[`docs/REFINEMENT.md`](docs/REFINEMENT.md).

## Layout (where to look)

| Path | Role |
|------|------|
| `include/hound/` | Core + HTTP/ingest headers (core must stay HTTP/CSV-free) |
| `src/main.cpp` | Server entrypoint |
| `tests/` | Catch2 unit, golden, integration, TSan |
| `benchmarks/micro/` | Google Benchmark |
| `benchmarks/macro/` | hey HTTP load |
| `benchmarks/profiling/` | perf / flamegraph helpers |
| `scripts/` | `run_correctness`, `run_micro`, `run_macro`, `compare_bench` |
| `baselines/micro_baseline.json` | Versioned micro performance baseline |

## Build rules

- CMake + **C++20**
- **Never** run benchmarks under sanitizers (distorts numbers)
- TSan is a **separate** configure: `-DHOUND_ENABLE_TSAN=ON` (disables benches)

```bash
# Default / correctness
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j && ctest --test-dir build --output-on-failure

# Release benches
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DHOUND_BUILD_BENCH=ON -DHOUND_ENABLE_TSAN=OFF
```

Keep dependencies light (Catch2, cpp-httplib, nlohmann/json, Google Benchmark
via FetchContent; `hey` / `perf` are external tools).

## When to run what

| Change type | Run |
|-------------|-----|
| Any code change touching index/API/scoring | `./scripts/run_correctness.sh` |
| Fuzzy structures, scoring, ingest hot path | `./scripts/run_micro.sh` then `./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_*.json` |
| HTTP handlers / concurrency under load | `./scripts/run_macro.sh` |
| Hunting CPU/cache hotspots | `benchmarks/profiling/perf_stat.sh` / `flamegraph.sh` (see that README) |

### `compare_bench.py`

Compares two Google Benchmark JSON files. Default gate: **+10%** `cpu_time`.
Exit `1` = regression. It does **not** run benches by itself — run micro first.

Only run `./scripts/save_baseline.sh …` when intentionally accepting a new
performance baseline (human decision).

Smoke for the comparer: `./scripts/test_compare_bench.sh`.

### Macro vs micro

- **Micro** = in-process core latency (µs/ms)
- **Macro** = live HTTP + JSON + loopback (do not compare to micro directly)
- Macro defaults to `hey -disable-keepalive` (keep-alive + httplib skewed hey)

Detail: [`benchmarks/macro/README.md`](benchmarks/macro/README.md),
[`benchmarks/profiling/README.md`](benchmarks/profiling/README.md).

## Coding constraints

1. **No business domain** in core or examples (no real schemas/PII).
2. Prefer header-light core; keep `FuzzyIndex` / trie / BK-tree free of HTTP/JSON wire types.
3. Public HTTP API: avoid breaking changes unless explicitly requested; justify first.
4. Prefer small, reviewable diffs; tests with the behavior they cover.
5. Conventional commits when the user asks to commit.
6. Do not commit `build/`, `build-bench/`, `build-tsan/`, or `benchmarks/results/*` artifacts (gitignored).

## Performance work

Before large fuzzy refactors, follow [`docs/REFINEMENT.md`](docs/REFINEMENT.md):
measure (`perf` / micro) first. **SymSpell is the default** fuzzy backend
(~−99% on `BM_SearchFuzzy/20000/2` vs old BK). Ingest is slower and RSS is
higher because of the symmetric-delete map — accepted in the versioned
`baselines/micro_baseline.json` (SymSpell). Force BK with `--fuzzy-backend bk`
or `HOUND_FUZZY_BACKEND=bk` when RAM/write churn matters more than fuzzy µs.

### Fuzzy backend use cases

| Backend | Use when | Avoid when |
|---------|----------|------------|
| **SymSpell** (default) | Read-heavy autocomplete after bulk/`prepare` | Continuous high-churn upserts; strict RAM budget |
| **BK-tree** | Low RAM, frequent rebuilds, parity/oracle tests | You need sub-10 µs fuzzy @ ~20k |

**Fuzzy PR gate metrics** (mandatory `compare_bench.py` names — see
`docs/REFINEMENT.md` §A2): `BM_SearchFuzzy/20000/1`,
`BM_SearchFuzzy/20000/2`, plus guards `BM_SearchExact/20000` and
`BM_Insert/20000`.

Any intentional micro regression >10% must be justified in the PR/commit
message or revert.
## Quick commands

```bash
./scripts/run_correctness.sh          # unit + golden + integration (+ TSan if libtsan present)
./scripts/run_micro.sh
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_<ts>.json
./scripts/run_macro.sh                # needs: go install github.com/rakyll/hey@latest
```
