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

## 5–8. Micro / macro / profiling / compare

Phases B–D (not implemented yet). See prior plan sections: Google Benchmark
JSON, hey HTTP load, `perf` wrappers, `compare_bench.py` with **+10%**
micro regression threshold.

---

## 9. Local “CI” (now)

| Job | When |
|-----|------|
| `./scripts/run_correctness.sh` | Before every merge |
| Micro + compare | Phase B+ |
| Macro hey | Manual / Phase C |

---

## 10. Dependencies

| Dep | Notes |
|-----|-------|
| Catch2 | FetchContent (existing) |
| libtsan | OS package for TSan job |
| Google Benchmark / hey | Phases B/C |

---

## 11. Implementation phases

| Phase | Status |
|-------|--------|
| **A** Correctness + golden + TSan | **Done** (2026-07-23) |
| **B** Micro (Google Benchmark) | Pending |
| **C** Macro (hey) | Pending |
| **D** Profiling + compare script | Pending |

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
