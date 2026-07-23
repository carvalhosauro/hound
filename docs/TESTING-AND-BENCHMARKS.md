# Hound — testing and benchmarks

Architecture for the correctness + performance suite.

Locked decisions (2026-07-23):

| Topic | Choice |
|-------|--------|
| Test framework | **Catch2** (already in tree; do not migrate to GoogleTest) |
| Microbenchmark | **Google Benchmark** (native JSON, params, scale curve) |
| Macrobenchmark HTTP | **hey** |
| Remote CI | **Deferred** — local scripts + docs first; GitHub Actions later |
| Sanitizers | Correctness/concurrency builds only; **never** on bench builds |
| Datasets | Fixed seed; golden fixtures versioned in-repo |

Related: [PLANO.md](PLANO.md), [REFINEMENT.md](REFINEMENT.md).

---

## 1. Goals

1. Prove correctness (fuzzy, merge, golden recall/MRR, concurrency).
2. Measure isolated (micro) and realistic HTTP (macro) performance.
3. Find bottlenecks (`perf` / flamegraphs / Valgrind when appropriate).
4. Compare baseline vs current with a documented regression threshold.
5. Keep numbers clean: separate builds, fixed seeds, minimal deps.

---

## 2. Layout

```
hound/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── golden/
│   │   ├── corpus.csv
│   │   ├── cases.csv
│   │   └── test_golden.cpp
│   └── concurrency/
│       └── test_tsan_rw.cpp
├── benchmarks/
│   ├── synth_generator/
│   ├── micro/          # Phase B
│   ├── macro/          # Phase C (hey)
│   ├── profiling/      # Phase D
│   └── results/        # generated (gitignored)
├── scripts/
│   └── run_correctness.sh   # Phase A
├── baselines/          # Phase B+
└── docs/TESTING-AND-BENCHMARKS.md
```

---

## 3. CMake builds

| Configure | Flags | Targets |
|-----------|-------|---------|
| **Release** | no sanitizers | `hound`, benches |
| **TSan** | `-DHOUND_ENABLE_TSAN=ON` → `-fsanitize=thread` | `hound_tests_tsan` only (benches forced off) |
| **Debug** | default correctness | `hound_tests` (unit + golden + integration) |

Requires system **libtsan** (e.g. Fedora: `dnf install libtsan`).

```bash
./scripts/run_correctness.sh
# or skip TSan:
HOUND_RUN_TSAN=0 ./scripts/run_correctness.sh
```

---

## 4. Correctness tests (Phase A)

### Unit (fuzzy + merge)

Edge cases: empty index, 1-char query, prefix autocomplete, typo within /
beyond distance, Unicode byte dropping (MVP ASCII), erase missing id,
duplicate upsert, score-merger alpha / ties / empty.

### Golden

- `tests/golden/corpus.csv` + `cases.csv` versioned.
- Reports **recall@k** and **MRR**; asserts recall ≥ 0.95 and MRR ≥ 0.80.

### Concurrency + TSan

- Parallel search threads + upsert/erase writer for ~800 ms.
- `FuzzyIndex` uses `std::shared_mutex` so the race is defined and TSan-clean.

---

## 5. Microbenchmark (Google Benchmark) — Phase B

Target: `hound_bench_micro` (`benchmarks/micro/bench_ops.cpp`).

| Bench | Params |
|-------|--------|
| `BM_Insert` | N ∈ {1k, 5k, 20k} |
| `BM_SearchExact` | N ∈ {1k, 5k, 20k} |
| `BM_SearchFuzzy` | N × distance ∈ {1,2,3} |
| `BM_ScoreMerge` | candidate set {64, 256, 1024} |

```bash
./scripts/run_micro.sh                          # → benchmarks/results/micro_<ts>.json
./scripts/save_baseline.sh benchmarks/results/micro_<ts>.json
```

- Release build in `build-bench/` (no sanitizers).
- Synthetic generator seed **42** (typo seed 99).
- Versioned baseline: `baselines/micro_baseline.json`.

## 6. Macrobenchmark (hey) — Phase C

HTTP load against a live sidecar. See [`benchmarks/macro/README.md`](../benchmarks/macro/README.md).

```bash
./scripts/run_macro.sh
# → benchmarks/results/macro_<ts>.txt
```

Includes loopback + HTTP + JSON — **not** comparable to micro µs.

## 7. Profiling — Phase D

See [`benchmarks/profiling/README.md`](../benchmarks/profiling/README.md).

```bash
./benchmarks/profiling/perf_stat.sh --benchmark_filter=BM_SearchFuzzy/20000/2
FLAMEGRAPH_DIR=~/src/FlameGraph ./benchmarks/profiling/flamegraph.sh
```

## 8. Baseline compare — Phase D

```bash
./scripts/run_micro.sh
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_<ts>.json
# Smoke (expects FAIL then OK):
./scripts/test_compare_bench.sh
```

Default gate: **+10%** on `cpu_time` (normalized by `time_unit`). Exit `1` on
regression. Rationale: local machine noise; avoid flaky fails on 2–3% jitter.

---

## 9. Local “CI” (now)

| Job | When |
|-----|------|
| `./scripts/run_correctness.sh` | Before every merge |
| `./scripts/run_micro.sh` (+ optional save_baseline) | Before perf-sensitive merges |
| `./scripts/run_macro.sh` | Manual / before HTTP path changes |
| `./scripts/compare_bench.py` | After micro runs / before perf merges |
| `./benchmarks/profiling/*` | When hunting CPU/cache hotspots |

---

## 10. Dependencies

| Dep | Notes |
|-----|-------|
| Catch2 | FetchContent (existing) |
| libtsan | OS package for TSan job |
| Google Benchmark | FetchContent when `HOUND_BUILD_BENCH` (Phase B) |
| hey | External binary (`go install github.com/rakyll/hey@latest`) |
| perf / FlameGraph | Optional OS + clone for Phase D |
| Python 3 | `compare_bench.py` (stdlib only) |

---

## 11. Implementation phases

| Phase | Status |
|-------|--------|
| **A** Correctness + golden + TSan | **Done** (2026-07-23) |
| **B** Micro (Google Benchmark) | **Done** (2026-07-23) |
| **C** Macro (hey) | **Done** (2026-07-23) |
| **D** Profiling + compare script | **Done** (2026-07-23) |

---

## 12. Changelog

### Phase A — 2026-07-23

- **Done:** expanded fuzzy/score unit edge cases; golden corpus + cases with
  recall@k/MRR; TSan concurrency test; `FuzzyIndex` `shared_mutex` (required
  for defined concurrent search+upsert); `scripts/run_correctness.sh`; CMake
  `HOUND_ENABLE_TSAN` disables benches in that configure.
- **How to run:** `./scripts/run_correctness.sh`
  Skip TSan: `HOUND_RUN_TSAN=0 ./scripts/run_correctness.sh`
- **Limits:** remote CI still deferred; micro/macro/compare not in Phase A.
  Docs are English (`REFINEMENT.md`, `TESTING-AND-BENCHMARKS.md`, `PLANO.md`).
  TSan job requires the OS `libtsan` package; the script skips with a warning
  if the runtime is missing (`sudo dnf install libtsan` on Fedora).

### Phase B — 2026-07-23

- **Done:** Google Benchmark micro suite (`hound_bench_micro`): insert, exact
  search, fuzzy distances 1–3, score merge; parametrized by index size;
  `scripts/run_micro.sh` + `scripts/save_baseline.sh`; versioned
  `baselines/micro_baseline.json` (seed 42).
- **How to run:** `./scripts/run_micro.sh` then optionally
  `./scripts/save_baseline.sh benchmarks/results/micro_….json`
- **Limits:** no automated compare/regression gate yet (Phase D); legacy
  `hound_bench` MVP binary still present alongside micro.

### Phase C — 2026-07-23

- **Done:** HTTP macrobenchmark with `hey` (`benchmarks/macro/run_macro.sh`,
  wrapper `scripts/run_macro.sh`); scenarios `/health`, exact `/search`, typo
  `/search`; synthetic corpus with fixed seed; README documents network/JSON
  overhead vs micro. Default `hey -disable-keepalive` (keep-alive + httplib
  inflated hey timings by ~40 ms). `/search` no longer holds the API write
  mutex (relies on `FuzzyIndex` shared locking).
- **How to run:** `GOBIN=$(go env GOPATH)/bin go install github.com/rakyll/hey@latest`
  then `./scripts/run_macro.sh` (optional `HOUND_MACRO_N`, `HOUND_MACRO_C`,
  `HOUND_MACRO_DOCS`).
- **Limits:** text report only (not JSON); not gated in CI; remote CI still deferred.
  Without keep-alive, p99 may show connection-churn outliers — prefer p50/p90.

### Phase D — 2026-07-23

- **Done:** `benchmarks/profiling/{perf_stat,flamegraph}.sh` + README (perf vs
  Valgrind); `scripts/compare_bench.py` with default **+10%** `cpu_time` gate;
  `scripts/test_compare_bench.sh` smoke (artificial +20% must FAIL).
- **How to run:**
  `./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_….json`
  Profiling requires `perf` (+ optional FlameGraph clone).
- **Limits:** remote CI still deferred; compare is local/manual; flamegraph
  needs `FLAMEGRAPH_DIR`.
