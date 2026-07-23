# Hound — post-MVP refinement

Living roadmap: measure first, ship small, prove each win with numbers.
Feature work (SymSpell, ranking, etc.) starts only after Phase A evidence.
Phase A is **done** (2026-07-23): `perf` confirms BK/Levenshtein dominate
fuzzy search @ 20k. Earlier testing work also added `shared_mutex` on
`FuzzyIndex` so concurrent search+upsert is defined under TSan.

Learning sources: Sonic, Typesense, Xapian, SymSpell, CppCon 2024
“When Nanoseconds Matter” (David Gross).

How to run the suite day-to-day: [`AGENTS.md`](../AGENTS.md),
[`benchmarks/macro/README.md`](../benchmarks/macro/README.md),
[`benchmarks/profiling/README.md`](../benchmarks/profiling/README.md).

---

## Working rules (non-negotiable)

1. **No guessing.** Every performance or quality claim needs before/after
   metrics. “Should be faster” is not enough to merge.
2. **One small delivery at a time.** Prefer a slice that lands in hours/days
   over a multi-week mega-refactor. Each delivery has an explicit DoD below.
3. **Benchmark for every change.** Prefer tracked micro benches
   (`BM_*` in `benchmarks/micro/`). If the hypothesis needs a one-off probe,
   a **temporary local script** under `scripts/tmp/` or
   `benchmarks/results/scratch/` is fine — keep it **gitignored / untracked**,
   document the command + numbers in the Phase 2 changelog, then delete or
   promote it once the insight is captured.
4. **Correctness before speed.** `./scripts/run_correctness.sh` must pass
   for any change that touches index, scoring, API, or concurrency.
5. **Regression gate.** Micro: `compare_bench.py` default **+10%** `cpu_time`
   vs `baselines/micro_baseline.json`. Intentional regressions need justification
   in the PR/commit message (or revert). Do not run benches under sanitizers.

---

## Measurement toolbox (current)

| Layer | What | How |
|-------|------|-----|
| Correctness | unit + golden + integration (+ TSan if present) | `./scripts/run_correctness.sh` |
| Micro | in-process core latency (Insert / SearchExact / SearchFuzzy / ScoreMerge) | `./scripts/run_micro.sh` → JSON under `benchmarks/results/` |
| Compare | relative slowdown vs versioned baseline | `./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_<ts>.json` |
| Macro | live HTTP + JSON + loopback (`hey`) | `./scripts/run_macro.sh` |
| Profile | CPU / flamegraph hotspot attribution | `benchmarks/profiling/perf_stat.sh`, `flamegraph.sh` |

**Micro ≠ macro.** Do not compare µs from Google Benchmark to HTTP `hey`
latencies. Use micro for core structure changes; macro for handler /
concurrency / serialization under load.

**Artifacts:** `build/`, `build-bench/`, `build-tsan/`, and
`benchmarks/results/*` stay untracked. Only update
`baselines/micro_baseline.json` via `./scripts/save_baseline.sh` when a human
**intentionally** accepts a new baseline.

### Delivery checklist (every phase slice)

Copy into the PR / changelog entry:

```text
Hypothesis:
Primary metric(s):          # e.g. BM_SearchFuzzy/20000/2 cpu_time
Secondary metric(s):        # recall@k, RSS, macro p99, …
Before (command + numbers):
After  (command + numbers):
Correctness: ./scripts/run_correctness.sh — pass / fail
Micro gate:  compare_bench.py — pass / intentional regression (why)
DoD items:   [ ] …
Decision:    ship / iterate / revert
```

Temporary probe template (untracked):

```bash
mkdir -p scripts/tmp
# scripts/tmp/probe_<topic>.sh  — local only; never commit
# print: scenario, N, metric, before/after paths
```

---

## Phase 0 — Baseline (2026-07-23)

Historical core numbers (synthetic dataset, fixed seed). Kept as the
qualitative “before” picture; day-to-day gates use the micro JSON baseline.

| size | ingest_ms | p50_us | p95_us | p99_us | recall@10 | rss_mb |
|------|-----------|--------|--------|--------|-----------|--------|
| 1 000 | 2.75 | 63.1 | 91.6 | 108.1 | 1.00 | 6.0 |
| 5 000 | 20.0 | 252.4 | 432.1 | 510.5 | 1.00 | 12.1 |
| 20 000 | 99.3 | 1026.6 | 1965.7 | 2331.6 | 1.00 | 33.7 |

Notes:

- Recall@10 = 1.0 on the happy path (unique texts + distance-1 typos).
- Latency grows with N; at 20k, p50 was already ~1 ms on that harness.
- Versioned micro baseline: `baselines/micro_baseline.json`
  (`BM_Insert`, `BM_SearchExact`, `BM_SearchFuzzy`, `BM_ScoreMerge`).

### Current architecture (audited)

| Piece | Implementation | File |
|-------|----------------|------|
| Prefix | Trie with per-node `unique_ptr` + `unordered_map<char,…>` | `include/hound/trie.hpp` |
| Fuzzy | `FuzzyBackend`: default BK-tree; optional SymSpell symmetric-delete | `fuzzy_backend.hpp`, `symspell_backend.hpp`, `bk_tree.hpp` |
| Orchestration | Sync upsert into Trie + FuzzyBackend + doc map | `include/hound/fuzzy_index.hpp` |
| Ranking | Linear `α·text + (1-α)·norm(external)` | `include/hound/score_merger.hpp` |
| Concurrency | `shared_mutex` on `FuzzyIndex`; HTTP still has an API-level mutex for snapshot writes | `fuzzy_index.hpp`, `http_api.hpp` |
| Persistence | Full binary snapshot rebuild on load | `include/hound/snapshot.hpp` |
| JSON API | Returns `id`, `score`, `text_relevance`, `external_score` | `/search` |

---

## Phase 0 — Learning map → current state

### 1. Sonic

| Learning | Status | Evidence |
|----------|--------|----------|
| Periodic background consolidation | **Not applied** | Upsert updates Trie+BK immediately; HTTP may snapshot under lock |
| API returns IDs only; business outside core | **Partial** | Core is domain-agnostic; `/search` still returns scores; merge runs inside sidecar |
| Typo tolerance ∝ term length | **Not applied** | Fixed `max_edit_distance` (default 2) |

### 2. Typesense

| Learning | Status | Evidence |
|----------|--------|----------|
| ART + leaf posting lists | **Not applied** | Classic trie; postings = `unordered_set` of ids |
| Worth migrating ART at N ~ thousands? | **Not now** | Dominant cost is BK-tree/Levenshtein, not prefix walk |
| Tie-break ranking pipeline | **Not applied** | Linear `ScoreMerger` only |

### 3. Xapian

| Learning | Status | Evidence |
|----------|--------|----------|
| Pluggable weighting model | **Not applied** | Concrete `ScoreMerger` |
| Compressed on-disk postings + B-tree | **Future** | In-memory + full snapshot today |

### 4. SymSpell

| Learning | Status | Evidence |
|----------|--------|----------|
| Symmetric delete | **Implemented behind flag (B2/B3)** — default still BK | `SymSpellFuzzyBackend`; CLI/env opt-in; ~98% faster fuzzy@20k/d=2; insert slower |
| Compound / word split | **Not applied** | Normalizer collapses spaces only |
| Practical distance ~2–3 | **Aligned** | Default max distance 2 |

### 5. “When Nanoseconds Matter”

| Learning | Status | Evidence |
|----------|--------|----------|
| Measure before optimizing | **Done (Phase A)** | `perf_stat` + flamegraph @ `BM_SearchFuzzy/20000/2`; see changelog |
| Cache-friendly layout | **Not applied** | Pointer-chasing nodes |
| Updates must not block searches | **Improved** | `shared_mutex` allows concurrent readers; writers still exclusive (no double-buffer / Sonic queue yet) |

---

## Phased roadmap (small, measurable deliveries)

Order is intentional: **profile → biggest fuzzy win → quality knobs →
ranking extensibility → concurrency → only then exotic structures**.
Skip or reorder only when Phase A numbers force it (document why).

### Shared DoD (applies to every delivery below)

- [ ] Hypothesis written (what improves, why, which metric).
- [ ] Before metrics captured (command + artifact path or pasted table).
- [ ] After metrics captured the same way.
- [ ] `./scripts/run_correctness.sh` green.
- [ ] Micro `compare_bench.py` green **or** intentional regression justified.
- [ ] Phase 2 changelog entry filled (before/after + decision).
- [ ] Temporary probes either deleted or promoted to tracked benches.

---

### Phase A — Evidence before structure work ✅ (2026-07-23)

**Goal:** Know where CPU goes at N≈20k fuzzy search so later work is guided
by data, not folklore.

| ID | Delivery | Measure | Done when | Status |
|----|----------|---------|-----------|--------|
| **A1** | Run `perf_stat` (+ flamegraph) on fuzzy search @ 20k | top symbols / %CPU; cycles | Changelog: top ≥3 hotspots with %; confirm/reject “BK/Levenshtein dominates” | **Done** — confirmed |
| **A2** | Freeze “gate metrics” for fuzzy work | `BM_SearchFuzzy/20000/{1,2}` (+ exact/insert guards) | Document mandatory micro names for fuzzy PRs | **Done** — see below |

**Exit Phase A:** Hotspot attribution recorded; no large structure change
merged without A1. ✅

#### A1 — Hotspot attribution (`BM_SearchFuzzy/20000/2`)

Commands (Release `build-bench/`, no sanitizers):

```bash
./benchmarks/profiling/perf_stat.sh \
  --benchmark_filter=BM_SearchFuzzy/20000/2 --benchmark_min_time=1.0

FLAMEGRAPH_DIR=/path/to/FlameGraph ./benchmarks/profiling/flamegraph.sh \
  --benchmark_filter=BM_SearchFuzzy/20000/2 --benchmark_min_time=2s
```

`perf report` / folded leaf samples (process-wide; fixture setup included):

| Rank | Symbol | ~%CPU (leaf) |
|------|--------|--------------|
| 1 | `hound::levenshtein` | **~65%** |
| 2 | `hound::BkTree::search_rec` | **~18%** |
| 3 | allocator (`malloc_consolidate` / free path) | **~3–6%** |

**Verdict:** **Confirmed** — BK-tree + Levenshtein dominate fuzzy search at N≈20k
(~83% combined leaf). Trie / exact path is not the bottleneck. Phase B
(SymSpell / symmetric-delete) remains the correct #1 structure bet.

`perf stat` (same filter, ~3.2s wall): ~10.5B cycles, ~28.3B instructions,
IPC ≈ 2.7; cache-miss rate ≈ 50% of cache-references (see Phase 2 changelog).

#### A2 — Mandatory gate metrics (fuzzy PRs)

Any PR that changes fuzzy structures, BK/SymSpell backends, edit distance,
or the `FuzzyIndex` search/upsert hot path **must** report
`compare_bench.py` (default +10% `cpu_time` vs
`baselines/micro_baseline.json`) for **all** of:

| Role | Micro name(s) | Why |
|------|---------------|-----|
| **Primary** | `BM_SearchFuzzy/20000/1`, `BM_SearchFuzzy/20000/2` | Target fuzzy latency at Phase-0 scale; d=1 and default d=2 |
| **Guard (exact)** | `BM_SearchExact/20000` | Catch accidental prefix/exact regressions |
| **Guard (ingest)** | `BM_Insert/20000` | Catch build-cost blowups from new fuzzy indexes |

Optional but encouraged when the change touches wider surfaces:
`BM_SearchFuzzy/20000/3`, `BM_SearchFuzzy/5000/{1,2}`, `BM_ScoreMerge/*`.

Paste the comparer table (or failing names) in the PR / Phase 2 changelog.
Do **not** claim a fuzzy win without the two primary names above.

Baseline reference (versioned `baselines/micro_baseline.json`, cpu_time):

| name | cpu_time |
|------|----------|
| `BM_SearchFuzzy/20000/1` | ~110 µs |
| `BM_SearchFuzzy/20000/2` | ~802 µs |
| `BM_SearchExact/20000` | ~3.4 µs |
| `BM_Insert/20000` | ~103 ms |

---

### Phase B — SymSpell / symmetric-delete (split slices)

**Goal:** Cut fuzzy latency/scalability without tanking recall on the golden
path. Ship behind a clear seam so each slice is reviewable.

| ID | Delivery | Measure | Done when | Status |
|----|----------|---------|-----------|--------|
| **B1** | Design + interface only (`FuzzyBackend`); BK remains default | correctness only (no perf claim) | Compiles; tests prove BK path unchanged; no public API break | **Done** (2026-07-23) |
| **B2** | Symmetric-delete index build (edits ≤2) + lookup; behind flag/compile switch | build RSS/time; lookup correctness vs BK on golden set | Unit/golden: same hits as BK on agreed fixture **or** documented intentional diffs | **Done** (2026-07-23) |
| **B3** | Wire into `FuzzyIndex` search path (feature flag default off) | before/after `BM_SearchFuzzy/*`; temporary probe OK if untracked | Flag off = baseline parity; flag on shows target latency drop on 20k/d=2 | **Done** (flag wired; perf claim deferred to B4) |
| **B4** | Enable default **only if** metrics win | micro + recall@10 (or golden) | p50/p99 fuzzy improve vs Phase 0/micro baseline; recall@10 on happy path ≥ baseline; `save_baseline.sh` only after human accept | pending |
| **B5** | Remove or demote BK from hot path (keep as test oracle if useful) | micro + correctness | Dead code path gone or clearly non-default; suite still green | pending |

**B1 seam:** `include/hound/fuzzy_backend.hpp` (`FuzzyBackend` + `BkFuzzyBackend` +
`FuzzyBackendKind`). `FuzzyIndex` takes `unique_ptr<FuzzyBackend>` (default BK).

**B2/B3 SymSpell:** `include/hound/symspell_backend.hpp` (`SymSpellFuzzyBackend`,
`make_fuzzy_backend`, env `HOUND_FUZZY_BACKEND`, CLI `--fuzzy-backend`).
Compile switch: `-DHOUND_DEFAULT_FUZZY_BACKEND_SYMSPELL`. Default remains **BK**.
Parity: unit fixture + golden corpus hit-id sets at `max_edit_distance=2`.
Intentional scope: dictionary deletes indexed to ≤2; d>2 vs BK not claimed equal.

**Target (hypothesis to validate, not a promise):** large drop on
`BM_SearchFuzzy/20000/2` vs current baseline. A1 confirmed Lev/BK dominate
(~83% leaf); a successful SymSpell path should move that share first. Next:
measure SymSpell-on micro (B4 gate) before flipping default.

---

### Phase C — Adaptive edit distance

**Goal:** Typo tolerance scales with term length (Sonic-style), without
surprising short-token explosions.

| ID | Delivery | Measure | Done when |
|----|----------|---------|-----------|
| **C1** | Spec: length → max distance table + tests | golden cases (short/medium/long) | Table documented; unit tests lock behavior |
| **C2** | Implement + optional API override | micro fuzzy + quality fixture | Default behavior documented; override preserves old fixed-d tests; no >10% micro regression unexplained |

---

### Phase D — Pluggable ranking

**Goal:** Replace hard-wired `ScoreMerger` with a small interface; keep
linear merge as default.

| ID | Delivery | Measure | Done when |
|----|----------|---------|-----------|
| **D1** | `Ranker` interface + adapt current merger | unit + `BM_ScoreMerge` | Same scores as today on golden; ScoreMerge micro within gate |
| **D2** | Typesense-style tie-break ranker (optional) | ranking fixture (order stability) | Fixture documents expected order; default ranker unchanged unless opted in |
| **D3** | Wire optional ranker through HTTP (if needed) | macro smoke / integration | Query param or config documented; no breaking default JSON |

---

### Phase E — Concurrency beyond `shared_mutex`

**Goal:** Writers do not stall readers under mixed load (Sonic-like).

| ID | Delivery | Measure | Done when |
|----|----------|---------|-----------|
| **E1** | Macro/mixed-load probe (tmp script OK): R-heavy + occasional upsert | hey / custom: search p99 during writes | Baseline contention numbers recorded |
| **E2** | Double-buffer or publish/swap design spike (minimal impl) | same probe + TSan | Search p99 under writes improves vs E1; TSan clean; correctness green |
| **E3** | Background consolidation (Sonic-style) — only if E2 insufficient | write churn + search latency | Changelog shows why E2 was not enough; metrics for consolidate interval |

---

### Phase F — Layout / ART / on-disk (only if profile demands)

**Do not start from fashion.** Gate on Phase A + post-SymSpell profile.

| ID | Delivery | Measure | Done when |
|----|----------|---------|-----------|
| **F0** | Re-profile after Phase B | perf / micro | Written decision: trie/layout is / is not the bottleneck |
| **F1** | Contiguous trie nodes **or** ART — pick one experiment | micro SearchExact + RSS | Improvement on the **profiled** bottleneck; otherwise abandon |
| **F2** | On-disk compressed postings | snapshot load time, RSS, search | Justified by rebuild/RSS pain with numbers |

---

### Phase G — Product polish (optional, non-blocking)

| ID | Delivery | Measure | Done when |
|----|----------|---------|-----------|
| **G1** | Optional `fields=id` projection | integration | Default response unchanged; projection returns ids only |
| **G2** | SymSpell compound / word-split | case fixtures + micro | Fixtures pass; cost documented; default off until measured |

---

## Priority ↔ phase map (legacy P-list)

| Old # | Theme | Phase slices |
|-------|-------|--------------|
| **P0** | `perf` profile @ 20k | **A1–A2** |
| **P1** | SymSpell / symmetric delete | **B1–B5** |
| **P2** | Adaptive edit distance | **C1–C2** |
| **P3** | Pluggable `Ranker` | **D1** |
| **P4** | Tie-break ranker | **D2–D3** |
| **P5** | Double-buffer / non-blocking writers | **E1–E2** |
| **P6** | Background consolidation | **E3** |
| **P7** | SymSpell compound splitting | **G2** |
| **P8** | Contiguous layout / ART | **F0–F1** |
| **P9** | On-disk index | **F2** |

---

## Phase 2 — Changelog

### 2026-07-23 — Phase B2/B3 SymSpell backend behind flag

```text
Hypothesis: Symmetric-delete lookup cuts BM_SearchFuzzy/20000/2 CPU vs BK
            while preserving hit ids on the golden fixture (d≤2).
Primary metric(s):   BM_SearchFuzzy/20000/2 cpu_time (BK vs SymSpell)
Secondary metric(s): BM_SearchFuzzy/20000/1; BM_Insert/20000; golden hit-id parity
Before: HOUND_FUZZY_BACKEND unset (BK) → micro_b3_bk.json
After:  HOUND_FUZZY_BACKEND=symspell → micro_b3_symspell.json
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  default path still BK (no baseline flip); SymSpell opt-in measured
DoD items:   [x] B2 SymSpell index+lookup  [x] parity vs BK d≤2
             [x] B3 flag default off (CLI/env)  [x] flag-on latency drop shown
Decision:    ship B2/B3 — do NOT flip default yet (B4); insert cost regresses hard
```

- Commands:
  - `./scripts/run_correctness.sh`
  - `./scripts/run_micro.sh benchmarks/results/micro_b3_bk.json`
  - `HOUND_FUZZY_BACKEND=symspell ./scripts/run_micro.sh benchmarks/results/micro_b3_symspell.json`
  - `./scripts/compare_bench.py …/micro_b3_bk.json …/micro_b3_symspell.json`
- Metrics (cpu_time, same host):

  | metric | BK (flag off) | SymSpell (flag on) | Δ |
  |--------|---------------|--------------------|---|
  | `BM_SearchFuzzy/20000/1` | 111 µs | 2.52 µs | **−97.7%** |
  | `BM_SearchFuzzy/20000/2` | 885 µs | 18.1 µs | **−98.0%** |
  | `BM_SearchExact/20000` | 3.26 µs | 0.93 µs | −71% (d=0 dict lookup) |
  | `BM_Insert/20000` | 104 ms | 3817 ms | **+3553%** (delete index build) |

- Correctness: pass (unit + golden BK↔SymSpell hit ids + HTTP + TSan)
- Micro gate: N/A for default baseline (still BK); SymSpell is opt-in
- Decision: **ship** B2/B3; **B4 blocked** until insert cost is acceptable or
  amortized (lazy/build-once). Do not `save_baseline.sh` / flip default yet.
- Notes: `--fuzzy-backend symspell` or `HOUND_FUZZY_BACKEND=symspell`;
  compile `-DHOUND_DEFAULT_FUZZY_BACKEND_SYMSPELL`. Micro benches honor the env.

### 2026-07-23 — Phase B1 FuzzyBackend seam (BK default)

```text
Hypothesis: Introduce FuzzyBackend so SymSpell can plug in later without
            changing FuzzyIndex call sites; default BkFuzzyBackend keeps
            today’s BK behavior (correctness only — no perf claim).
Primary metric(s):   correctness (unit/golden/integration/TSan)
Secondary metric(s): same-machine micro before/after B1 (gate names)
Before: pre-B1 micro on this host (micro_20260723T174922Z.json)
After:  post-B1 micro (micro_20260723T174948Z.json)
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  same-machine compare_bench — pass (virtual dispatch noise ≪ 10%)
DoD items:   [x] interface + Bk adapter  [x] BK path tests  [x] no public API break
Decision:    ship — proceed to B2 (symmetric-delete behind flag)
```

- Commands:
  - `./scripts/run_correctness.sh`
  - same-machine: `compare_bench.py micro_…174922Z.json micro_…174948Z.json`
- Metrics (same machine, cpu_time Δ):

  | metric | before | after | Δ |
  |--------|--------|-------|---|
  | `BM_SearchFuzzy/20000/1` | 134.7 µs | 127.3 µs | −5.5% |
  | `BM_SearchFuzzy/20000/2` | 1017 µs | 1004 µs | −1.4% |
  | `BM_SearchExact/20000` | 3.97 µs | 3.98 µs | +0.3% |
  | `BM_Insert/20000` | 133.7 ms | 141.9 ms | +6.1% |

- Note: vs **versioned** `baselines/micro_baseline.json` this host is currently
  ~15–30% slower even **before** B1 (ScoreMerge also moves) — treat as machine
  load variance, not B1. Do not `save_baseline.sh` from this run.
- Correctness: pass
- Micro gate: pass (same-machine)
- Decision: **ship**
- Notes: `FuzzyIndex(std::unique_ptr<FuzzyBackend>)` is additive; default ctor
  unchanged for callers.

### 2026-07-23 — Phase A Evidence before structure work

```text
Hypothesis: At N≈20k fuzzy search, BK-tree + repeated Levenshtein dominate
            CPU; trie/prefix is not the primary bottleneck.
Primary metric(s):   BM_SearchFuzzy/20000/2 leaf %CPU (perf / flamegraph)
Secondary metric(s): perf stat cycles/instructions/cache; BM_SearchFuzzy/20000/1
Before (command + numbers): Phase 0 micro baseline only (no prior perf attribution)
After  (command + numbers): see Commands + Metrics below
Correctness: N/A (docs + measurement only; no index/API code change)
Micro gate:  N/A (no code change; gate names frozen in A2)
DoD items:   [x] A1 hotspots ≥3 with %  [x] A1 confirm/reject BK/Lev
             [x] A2 mandatory micro names documented  [x] changelog
Decision:    ship — Phase A exit met; proceed to Phase B guided by A1
```

- Commands:
  - `./benchmarks/profiling/perf_stat.sh --benchmark_filter=BM_SearchFuzzy/20000/2 --benchmark_min_time=1.0`
  - `FLAMEGRAPH_DIR=… ./benchmarks/profiling/flamegraph.sh --benchmark_filter=BM_SearchFuzzy/20000/2 --benchmark_min_time=2s`
  - Artifact (local, gitignored): `benchmarks/results/perf_20260723T173804Z.{data,folded,svg}`
- Metrics:

  | metric | value | notes |
  |--------|-------|-------|
  | `hound::levenshtein` leaf | ~65% | #1 hotspot |
  | `hound::BkTree::search_rec` leaf | ~18% | #2 hotspot |
  | allocator (`malloc_consolidate` …) | ~3% | #3; includes fixture teardown noise |
  | BK+Lev combined leaf | ~83% | **confirms** folklore |
  | `BM_SearchFuzzy/20000/2` cpu_time (this run) | ~942–964 µs | vs baseline ~802 µs (machine variance; not a gate run) |
  | cycles / instructions | ~10.5B / ~28.3B | IPC ≈ 2.7 |
  | cache-references / misses | ~96.5M / ~48.1M | miss rate ≈ 50% of refs |

- Correctness: N/A
- Micro gate: N/A
- Decision: **ship** — A1/A2 complete; SymSpell (Phase B) remains #1 priority
- Notes: Process-wide `perf` includes one-shot index build; longer
  `--benchmark_min_time` increases search share. Conclusion unchanged.

Template for later slices:

```markdown
### YYYY-MM-DD — <Phase ID> <title>

- Hypothesis:
- Commands:
  - before: …
  - after: …
- Metrics:

  | metric | before | after | Δ |
  |--------|--------|-------|---|
  | … | … | … | … |

- Correctness: pass
- Micro gate: pass | justified regression
- Decision: ship | iterate | revert
- Notes:
```

---

## Future decisions

1. On-disk compressed postings if snapshot rebuild/RSS hurts (**F2**).
2. Revisit ART only if trie dominates after SymSpell (**F0→F1**).
3. Optional `fields=id` projection without removing score fields (**G1**).
