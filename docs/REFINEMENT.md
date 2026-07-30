# Hound — post-MVP refinement

Living roadmap: measure first, ship small, prove each win with numbers.

Learning sources: Sonic, Typesense, Xapian, SymSpell, CppCon 2024
“When Nanoseconds Matter” (David Gross).

How to run the suite day-to-day: [`AGENTS.md`](../AGENTS.md),
[`benchmarks/macro/README.md`](../benchmarks/macro/README.md),
[`benchmarks/profiling/README.md`](../benchmarks/profiling/README.md).

---

## Status — where we stopped (2026-07-24)

**Paused after Phase E3.** Phases **A–D**, issue **#1**, **E1** (mixed-load
baseline), **E2** (opt-in publish-swap), and **E3** (opt-in background
consolidation with publish-swap) are done. Default process behavior is unchanged:
SymSpell + linear ranker + `shared_mutex` (no publish-swap, no consolidate).
Algorithm knobs stay **factories at boot / optional query overrides** — not
scattered `if`s.

| Milestone | Commits (local `main`) |
|-----------|------------------------|
| **D1–D3** | `515569e` Ranker+TieBreak · `65e8a46` CLI/HTTP · `9e0a490`/`f270991` docs |
| **#1** | `e669abd` uint32 postings · `7e35c17` tagged values · closed on GitHub |
| **E1** | probe + changelog (contention baseline) |
| **E2** | `f51563b` publish-swap opt-in · specs/plans under `docs/superpowers/` |
| **E3** | `86f3001` merge · `3c93c10`/`1a19549` core · `3d3f482` CLI · `0070b1b` TSan · docs/spec/plan |
| **Baseline** | `save_baseline.sh` 2026-07-26 (`micro_baseline_refresh_20260726T043405Z`) |

SymSpell @20k RSS ~**226 MB** (was ~418 MB before #1). Micro baseline refreshed
2026-07-26 (post-#1 Insert + current SymSpell defaults) via `save_baseline.sh`.

Follow-up test gaps (optional, not blockers):
[#2](https://github.com/carvalhosauro/hound/issues/2)–[#5](https://github.com/carvalhosauro/hound/issues/5).

### What we just finished (E1–E3)

| Slice | What | Outcome |
|-------|------|---------|
| **E1** | Mixed-load HTTP probe (search / write / mixed) | Contended `/search` p99 vs search-only: exact **+644 ms**, typo **+1087 ms** (E1 order caveat: write-only before mixed emptied hits) |
| **E2** | Opt-in publish-swap behind `--publish-swap` / `HOUND_PUBLISH_SWAP` | Same-day adjusted probe (search→mixed, preserve `doc-0`): mixed p99 **−122 / −94 ms** vs legacy; writes/s **16.5→3.8**; TSan clean; **default still `shared_mutex`** |
| **E3** | `--publish-swap --consolidate-ms N` / `HOUND_CONSOLIDATE_MS` | E2-sync vs E3 @ **200 ms**: mixed writes/s **3.7→246.2**; mixed p99 exact **1.03 s** vs **1.06 s**, typo **1.23 s** vs **1.06 s**; **default unchanged** |

**E2 tradeoff:** readers avoid `unique_lock`, but sync publish-swap does
SymSpell `prepare` + deep-copy on every live upsert → ~4 writes/s under mixed
search. **E3** batches publish on a timer when combined with publish-swap;
still opt-in only — do not flip defaults without a product decision.

### Algorithm selection pattern (how flags work)

Boot: env → CLI → construct `FuzzyIndex` with injected pieces. Per-request
overrides only where documented (`?ranker=`, `?max_edit_distance=`).

| Knob | Surfaces | Default | Effect |
|------|----------|---------|--------|
| Fuzzy backend | `--fuzzy-backend` / `HOUND_FUZZY_BACKEND` | SymSpell | `FuzzyBackend` impl (BK = oracle/escape) |
| Ranker | `--ranker` / `HOUND_RANKER` / `?ranker=` | linear | `Ranker` impl (`ScoreMerger` vs `TieBreakRanker`) |
| Concurrency | `--publish-swap` / `HOUND_PUBLISH_SWAP` | shared_mutex | `PublishMode::Legacy` vs `PublishSwap` |
| Consolidate interval | `--consolidate-ms` / `HOUND_CONSOLIDATE_MS` (requires publish-swap) | 0 (sync publish) | Defer publish; background flush every N ms |
| Edit distance | omit vs `?max_edit_distance=` | adaptive table | Phase C length→distance |

### Done (shipped)

| Phase | What landed | Outcome |
|-------|-------------|---------|
| **A** | `perf` + flamegraph @ `BM_SearchFuzzy/20000/2`; gate metrics frozen | Confirmed BK+Levenshtein ~83% CPU — guided SymSpell |
| **B1–B5** | `FuzzyBackend` seam → SymSpell → default → BK demoted to oracle | Fuzzy @ 20k/d=2 **~−99%** vs old BK; golden recall held |
| **C1–C2** | Adaptive edit distance by query length + HTTP/API override | Short queries no longer use d=2 by default |
| **D1–D3** | Pluggable `Ranker` → `TieBreakRanker` opt-in → CLI/HTTP wire | Default `ScoreMerger` + JSON unchanged; `?ranker=` / `--ranker` |
| **#1** | uint32 postings + tagged single/multi delete values | RSS @20k **~418→226 MB** (−46%); Insert/prepare faster; fuzzy OK; **closed** |
| **E1** | Mixed-load macro probe | Contended search p99 baseline recorded |
| **E2** | Opt-in publish-swap (`IndexState` + atomic publish) | Modest mixed p99 win; write path costly; default unchanged |
| **E3** | Background consolidation with publish-swap (`consolidate-ms`) | Mixed writes/s **~67×** vs sync swap @200 ms; p99 ~flat; opt-in only |
| **Baseline** | Human `save_baseline.sh` (2026-07-26) | `baselines/micro_baseline.json` — Insert/20k **~2703→1141 ms** (−58%); Fuzzy/20k/2 **~7.3→5.8 µs** |

**Accepted tradeoffs (documented):** SymSpell RSS/`prepare` vs BK; sync
publish-swap write cost vs reader stall; E3 trades staleness between
consolidations for write throughput. Prefer `--fuzzy-backend bk` or legacy
concurrency when RAM or immediate read-your-writes matters.

### Not started yet

| Phase | Theme | Notes |
|-------|-------|-------|
| **F** | Layout / ART / on-disk | Only if post-SymSpell profile demands |
| **G** | `fields=id`, SymSpell compound | Optional polish |
| **H** | Generic attrs + multi-index | **H1 attrs equality shipped** (flat routes, string wire); multi-index next — § Phase H |
| **Sync** | DB → Hound push patterns | Docs in § Sync + [`DX.md`](DX.md) D2; no connector in-core |
| **#2–#5** | Ranker test hardening | Optional; DoD for D already met |

### Suggested next steps (pick one)

1. **DX** — [`docs/DX.md`](DX.md): maturity **D7.1–D7.4** + **D7.6** Done;
   **D7.5** dogfood waits on real evidence. Optional: **D5** clients, **D4.1**.
2. **Product** — **H1 slice 2** multi-index design, or **H0** filter-after POC in a consumer.
3. **Polish** — [#2](https://github.com/carvalhosauro/hound/issues/2)–[#5](https://github.com/carvalhosauro/hound/issues/5) / **G1** `fields=id`.
4. **F0** — re-profile only if trie/layout is suspected after SymSpell wins.

Do not start ART/layout (**F1+**) without a new profile saying trie/layout is the
bottleneck. Do not flip publish-swap or consolidate defaults without a product
decision and fresh probe numbers.

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
| Fuzzy | **Default SymSpell**; BK demoted to oracle/escape (`bk_fuzzy_backend.hpp`) | `symspell_backend.hpp`, `bk_fuzzy_backend.hpp`, `bk_tree.hpp` |
| Orchestration | Sync upsert into Trie + FuzzyBackend + doc map | `include/hound/fuzzy_index.hpp` |
| Ranking | Pluggable `Ranker`; default `ScoreMerger`; optional `TieBreakRanker` via ctor / CLI / `?ranker=` | `ranker.hpp`, `ranker_factory.hpp`, `score_merger.hpp`, `tie_break_ranker.hpp` |
| Concurrency | Default `shared_mutex`; opt-in `PublishMode::PublishSwap` (`--publish-swap`) | `fuzzy_index.hpp`, `http_api.hpp` |
| Persistence | Full binary snapshot rebuild on load | `include/hound/snapshot.hpp` |
| JSON API | Returns `id`, `score`, `text_relevance`, `external_score` | `/search` |

---

## Phase 0 — Learning map → current state

### 1. Sonic

| Learning | Status | Evidence |
|----------|--------|----------|
| Periodic background consolidation | **Applied (E3 opt-in)** | `--publish-swap --consolidate-ms N`; default path still immediate publish / legacy mutex |
| API returns IDs only; business outside core | **Partial** | Core is domain-agnostic; `/search` still returns scores; merge runs inside sidecar |
| Typo tolerance ∝ term length | **Applied (Phase C)** | Adaptive table in `adaptive_edit_distance.hpp`; optional override |

### 2. Typesense

| Learning | Status | Evidence |
|----------|--------|----------|
| ART + leaf posting lists | **Not applied** | Classic trie; postings = `unordered_set` of ids |
| Worth migrating ART at N ~ thousands? | **Not now** | Pre-SymSpell bottleneck was BK/Lev; re-profile before ART (**F0**) |
| Tie-break ranking pipeline | **Applied (D2)** | `TieBreakRanker` opt-in; default still linear `ScoreMerger` |

### 3. Xapian

| Learning | Status | Evidence |
|----------|--------|----------|
| Pluggable weighting model | **Applied (D1–D3)** | `Ranker` + `ScoreMerger` default; `TieBreakRanker`; CLI/HTTP select |
| Compressed on-disk postings + B-tree | **Future** | In-memory + full snapshot today |

### 4. SymSpell

| Learning | Status | Evidence |
|----------|--------|----------|
| Symmetric delete | **Default (B4/B5)** — BK oracle/escape | ~−99% fuzzy@20k/d=2; RSS/`prepare` cut in [#1](https://github.com/carvalhosauro/hound/issues/1) (closed; ~418→226 MB @20k) |
| Compound / word split | **Not applied** | Normalizer collapses spaces only |
| Practical distance ~2–3 | **Aligned** | Default max distance 2 |

### 5. “When Nanoseconds Matter”

| Learning | Status | Evidence |
|----------|--------|----------|
| Measure before optimizing | **Done (Phase A)** | `perf_stat` + flamegraph @ `BM_SearchFuzzy/20000/2`; see changelog |
| Cache-friendly layout | **Not applied** | Pointer-chasing nodes |
| Updates must not block searches | **Partial (E2)** | Default still exclusive writers; opt-in publish-swap removes reader/`unique_lock` stall (write path costly) |

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

Baseline reference (versioned `baselines/micro_baseline.json`, cpu_time;
refreshed **2026-07-26** post-#1):

| name | cpu_time |
|------|----------|
| `BM_SearchFuzzy/20000/1` | ~1.70 µs |
| `BM_SearchFuzzy/20000/2` | ~5.82 µs |
| `BM_SearchExact/20000` | ~0.93 µs |
| `BM_Insert/20000` | ~1141 ms |

---

### Phase B — SymSpell / symmetric-delete ✅ (2026-07-23)

**Goal:** Cut fuzzy latency/scalability without tanking recall on the golden
path. Ship behind a clear seam so each slice is reviewable.

| ID | Delivery | Measure | Done when | Status |
|----|----------|---------|-----------|--------|
| **B1** | Design + interface only (`FuzzyBackend`); BK remains default | correctness only (no perf claim) | Compiles; tests prove BK path unchanged; no public API break | **Done** (2026-07-23) |
| **B2** | Symmetric-delete index build (edits ≤2) + lookup; behind flag/compile switch | build RSS/time; lookup correctness vs BK on golden set | Unit/golden: same hits as BK on agreed fixture **or** documented intentional diffs | **Done** (2026-07-23) |
| **B3** | Wire into `FuzzyIndex` search path (feature flag default off) | before/after `BM_SearchFuzzy/*`; temporary probe OK if untracked | Flag off = baseline parity; flag on shows target latency drop on 20k/d=2 | **Done** (2026-07-23) |
| **B4** | Enable default **only if** metrics win | micro + recall@10 (or golden) | p50/p99 fuzzy improve vs Phase 0/micro baseline; recall@10 on happy path ≥ baseline; `save_baseline.sh` only after human accept | **Done** (2026-07-23) — SymSpell default; baseline **accepted** same day |
| **B5** | Remove or demote BK from hot path (keep as test oracle if useful) | micro + correctness | Dead code path gone or clearly non-default; suite still green | **Done** (2026-07-23) — BK in `bk_fuzzy_backend.hpp` as oracle/escape only |

**Hot path:** SymSpell (`symspell_backend.hpp`). **Oracle/fallback:** `BkFuzzyBackend`
in `bk_fuzzy_backend.hpp` + raw `BkTree` for unit oracles. Escape hatch unchanged
(`--fuzzy-backend bk`, env, `-DHOUND_DEFAULT_FUZZY_BACKEND_BK`).

**B4 metrics (same host):** `BM_SearchFuzzy/20000/2` SymSpell ~8 µs vs BK ~1.2 ms
(~−99%) and vs versioned baseline ~802 µs (~−99%). Golden recall unchanged.
`BM_Insert/20000` still ~10× BK after lazy+faster deletes — **intentional**.

**Baseline (human-accepted 2026-07-23):** `baselines/micro_baseline.json` refreshed
from SymSpell-default micro (`micro_20260723T200401Z.json`). Gate metrics in that
file: Insert/20k ~2703 ms; SearchFuzzy/20k/2 ~7.3 µs; SearchExact/20k ~0.91 µs.
Post-#1 same-host Insert/20k ~834 ms and RSS ~226 MB — optional human
`save_baseline.sh` if the gate should track the new Insert level.

### Fuzzy backends — use cases

| Backend | How to select | Prefer when | Cost profile (@ ~20k synthetic) |
|---------|---------------|-------------|----------------------------------|
| **SymSpell** | **Default**; `--fuzzy-backend symspell` | Bulk load → serve; query latency dominates | Search ~µs; `prepare`/ingest improved after #1; RSS ~**226 MB** @20k (was ~418 MB) |
| **BK-tree** | `--fuzzy-backend bk`, `HOUND_FUZZY_BACKEND=bk`, `-DHOUND_DEFAULT_FUZZY_BACKEND_BK` | RAM-constrained; frequent writes; test oracle | Search ~ms; ingest/RSS cheaper (~**30 MB** probe) |

Open follow-ups (roadmap only, no GitHub issue yet): denser open-addressing
delete map; incremental deletes on upsert — open an issue when scheduling work.

---

### Phase C — Adaptive edit distance ✅ (2026-07-23)

**Goal:** Typo tolerance scales with term length (Sonic-style), without
surprising short-token explosions.

| ID | Delivery | Measure | Done when | Status |
|----|----------|---------|-----------|--------|
| **C1** | Spec: length → max distance table + tests | golden cases (short/medium/long) | Table documented; unit tests lock behavior | **Done** (2026-07-23) |
| **C2** | Implement + optional API override | micro fuzzy + quality fixture | Default behavior documented; override preserves old fixed-d tests; no >10% micro regression unexplained | **Done** (2026-07-23) |

#### C1 — Length → max distance table

Documented in `include/hound/adaptive_edit_distance.hpp` and locked by
`tests/unit/test_adaptive_edit_distance.cpp`:

| normalized query length | `max_edit_distance` |
|-------------------------|---------------------|
| 0 … 2 | 0 |
| 3 … 5 | 1 |
| 6+ | 2 |

#### C2 — Wired into search

`SearchOptions::max_edit_distance` is `std::optional<int>`:
- **omit / nullopt** → adaptive table (default)
- **explicit value** → fixed override (unit/golden/micro keep passing `.max_edit_distance = N`)

HTTP: optional query param `max_edit_distance`; omit for adaptive.

---

### Phase D — Pluggable ranking ✅ (2026-07-23)

**Goal:** Replace hard-wired `ScoreMerger` with a small interface; keep
linear merge as default.

**Exit Phase D:** D1 seam + D2 optional tie-break + D3 CLI/HTTP wire; default
scores/JSON unchanged; correctness green. ✅

| ID | Delivery | Measure | Done when | Status |
|----|----------|---------|-----------|--------|
| **D1** | `Ranker` interface + adapt current merger | unit + `BM_ScoreMerge` | Same scores as today on golden; ScoreMerge micro within gate | **Done** (2026-07-23) |
| **D2** | Typesense-style tie-break ranker (optional) | ranking fixture (order stability) | Fixture documents expected order; default ranker unchanged unless opted in | **Done** (2026-07-23) |
| **D3** | Wire optional ranker through HTTP (if needed) | macro smoke / integration | Query param or config documented; no breaking default JSON | **Done** (2026-07-23) |

#### D1 — Ranker seam

- Interface: `include/hound/ranker.hpp` (`Ranker::rank(candidates, RankOptions)`).
- Default: `ScoreMerger` implements `Ranker`; `make_default_ranker()`.
- `FuzzyIndex` takes optional `unique_ptr<Ranker>` (same pattern as `FuzzyBackend`).
- `alpha` is per-call via `RankOptions` so a shared ranker stays safe under concurrent search.
- No public HTTP/JSON change.

#### D2 — TieBreakRanker (opt-in)

Typesense-style lexicographic order (default Typesense
`_text_match:desc,default_sorting_field:desc`):

| Priority | Field | Order |
|----------|-------|-------|
| 1 | `text_relevance` | desc |
| 2 | `external_score` | desc |
| 3 | `id` | asc |

- Header: `include/hound/tie_break_ranker.hpp` (`make_tie_break_ranker()`).
- `RankOptions::alpha` ignored; `hit.score` = `text_relevance` (primary key).
- Opt-in: `FuzzyIndex(make_default_fuzzy_backend(), make_tie_break_ranker())`.
- Default path unchanged (`make_default_ranker()` → `ScoreMerger`).
- Fixtures: `tests/unit/test_tie_break_ranker.cpp` lock equal-text / text-vs-external order
  and contrast vs linear `ScoreMerger`.

#### D3 — HTTP / CLI wire

| Surface | How | Default |
|---------|-----|---------|
| Process | `--ranker linear\|tie_break` or `HOUND_RANKER` | `linear` |
| Per-query | `GET /search?ranker=linear\|tie_break` | omit → process default |
| Factory | `ranker_factory.hpp` (`make_ranker`, `parse_ranker_kind`) | — |

- Invalid `?ranker=` → HTTP 400; response JSON fields unchanged when valid.
- `SearchOptions::ranker` is `optional<RankerKind>` (nullopt = index-owned ranker).
- Documented in README.
- Optional follow-ups (not D blockers):
  [#2](https://github.com/carvalhosauro/hound/issues/2) CLI env harness,
  [#3](https://github.com/carvalhosauro/hound/issues/3) HTTP aliases e2e,
  [#4](https://github.com/carvalhosauro/hound/issues/4) macro `hey` smoke,
  [#5](https://github.com/carvalhosauro/hound/issues/5) concurrent `?ranker=` / E1.

---

### Phase E — Concurrency beyond `shared_mutex`

**Goal:** Writers do not stall readers under mixed load (Sonic-like).

| ID | Delivery | Measure | Done when | Status |
|----|----------|---------|-----------|--------|
| **E1** | Macro/mixed-load probe (tmp script OK): R-heavy + occasional upsert | hey / custom: search p99 during writes | Baseline contention numbers recorded | **Done** (2026-07-24) — exact p99 **+644 ms**, typo **+1087 ms** vs search-only |
| **E2** | Double-buffer or publish/swap design spike (minimal impl) | same probe + TSan | Search p99 under writes improves vs E1; TSan clean; correctness green | **Done** (2026-07-24) — opt-in; mixed p99 −122/−94 ms vs legacy probe; writes/s 16.5→3.8 |
| **E3** | Background consolidation (Sonic-style) with publish-swap | write churn + search latency | E2 sync write cost addressed; interval documented | **Done** (2026-07-24) — `consolidate-ms=200`: mixed writes/s **3.7→246.2**; mixed p99 exact **1.03→1.06 s**, typo **1.23→1.06 s** |

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
| **P5** | Double-buffer / non-blocking writers | **E2** ✅ (opt-in; default unchanged) |
| **P6** | Background consolidation | **E3** ✅ (opt-in with publish-swap; default unchanged) |
| **P7** | SymSpell compound splitting | **G2** |
| **P8** | Contiguous layout / ART | **F0–F1** |
| **P9** | On-disk index | **F2** |
| — | SymSpell delete-map RSS/`prepare` | **#1** ✅ closed |

---

## Phase 2 — Changelog

### 2026-07-30 — H1 attrs equality micro trade-offs (Task 5)

```text
Hypothesis: Attr postings on upsert and AND-equality filter on search add modest
            cost vs no-attrs path; gate metrics (legacy names) stay within +10%.
Primary metric(s):   BM_Insert/20000; BM_SearchFuzzy/20000/{1,2}
Secondary metric(s): BM_InsertWithAttrs/20000; BM_SearchFuzzyFiltered/20000/{1,2}
Commands: cmake --build build-bench -j2 --target hound_bench_micro;
           hound_bench_micro → benchmarks/results/micro_20260730T041622Z.json;
           compare_bench.py vs baselines/micro_baseline.json
Correctness: N/A (bench-only)
Micro gate:  pass — max delta BM_SearchFuzzy/20000/2 +5.5% vs baseline
Decision:    ship — filter path ~2.6× fuzzy/2 at 20k; insert-with-attrs ~flat vs Insert/20k
```

| Metric | Unfiltered / no attrs | With attrs / filtered |
|--------|----------------------|------------------------|
| `BM_Insert/20000` | **1172 ms** (gate) | — |
| `BM_InsertWithAttrs/20000` | — | **1147 ms** |
| `BM_SearchFuzzy/20000/1` | **1.72 µs** (gate) | — |
| `BM_SearchFuzzyFiltered/20000/1` | — | **11.5 µs** |
| `BM_SearchFuzzy/20000/2` | **6.15 µs** (gate) | — |
| `BM_SearchFuzzyFiltered/20000/2` | — | **16.3 µs** |

- Baseline reference (versioned): Insert/20k ~1141 ms; Fuzzy/20k/1 ~1.70 µs;
  Fuzzy/20k/2 ~5.82 µs (`baselines/micro_baseline.json`).
- Insert with `tenant` attrs (64 values, `i % 64`) is within noise of plain Insert
  on this run (~−2% cpu_time).
- Filtered fuzzy (`attr_filters tenant="0"`, ~1/64 docs eligible) adds postings
  intersection after fuzzy candidate generation: ~6.7× vs unfiltered at D=1,
  ~2.6× at D=2 — expected (smaller eligible set does not skip SymSpell work).
- Gate legacy names unchanged in `compare_bench`; new benches recorded here only.

### 2026-07-26 — Accept post-#1 micro baseline

```text
Hypothesis: Human accepts current SymSpell-default micro (post-#1 Insert/RSS)
            as the versioned gate so compare_bench tracks real Insert level.
Primary metric(s):   baselines/micro_baseline.json from micro_baseline_refresh_*
Secondary metric(s): BM_SearchFuzzy/20000/{1,2}; SearchExact guards
Commands: ./scripts/run_micro.sh → save_baseline.sh micro_baseline_refresh_20260726T043405Z.json
Correctness: N/A (baseline promotion)
Micro gate:  new reference (vs old SymSpell pre-#1 baseline: Insert −58%, Fuzzy/2 −20%)
Decision:    ship — baseline updated
```

- Gate snapshot (cpu_time): Insert/20k ~**1141 ms**; SearchFuzzy/20k/1 ~**1.70 µs**;
  SearchFuzzy/20k/2 ~**5.82 µs**; SearchExact/20k ~**0.93 µs**
- vs previous versioned baseline (SymSpell default, pre-#1 Insert): Insert/20k
  **2703→1141 ms**; Fuzzy/20k/2 **7.32→5.82 µs**; Exact ~flat
- Decision: **ship**

### 2026-07-24 — Phase E3 background consolidation (publish-swap)

```text
Hypothesis: Deferring publish-swap prepare+clone until a periodic consolidate
            restores mixed write throughput without blowing mixed /search p99.
Primary metric(s):   mixed writes/s; hey mixed /search p99 exact+typo
Secondary metric(s): E2 sync swap baseline; correctness; default path unchanged
Before: benchmarks/results/e3_before_e2_sync_20260724T191328Z.txt
After:  benchmarks/results/e3_after_consolidate_200ms_20260724T191347Z.txt
Correctness: ./scripts/run_correctness.sh + TSan consolidate — pass (branch)
Micro gate:  quiet re-run — primary fuzzy + Insert pass; SearchExact/20000
             ±noise (~0.97–1.17 µs vs baseline ~0.91 µs; machine variance)
DoD items:   [x] opt-in flags  [x] probe  [x] changelog  [x] default unchanged
Decision:    ship — opt-in only; require --publish-swap; do not flip defaults
```

- Command: `HOUND_E3_CONSOLIDATE_MS=200 ./scripts/tmp/probe_e3_mixed_load.sh`
  (untracked wrapper → `probe_e2_mixed_load.sh`; `docs=5000`, `n=2000`, `c=50`).
- Surfaces: `--consolidate-ms`, `HOUND_CONSOLIDATE_MS` (meaningful only with
  `--publish-swap`); background thread flushes dirty draft on interval.
- Metrics (mixed scenario, same probe order as E2):

  | mode | writes/s | mixed p99 exact | mixed p99 typo |
  |------|----------|-----------------|----------------|
  | E2 sync `--publish-swap` | **3.7** | **1.026 s** | **1.228 s** |
  | E3 `--publish-swap --consolidate-ms 200` | **246.2** | **1.064 s** | **1.058 s** |

- Micro (legacy defaults): `BM_SearchFuzzy/20000/{1,2}` within gate; Insert faster
  (post-#1). Full `run_micro.sh` showed SearchExact noise under load_avg≈2;
  quiet repeats of `BM_SearchExact/20000` spanned 0.97–1.17 µs — not treated as
  an E3 regression (legacy path only adds a mode branch).
- E3 mixed exact `Size/request` dipped (~296 B vs ~619 B on sync run) — treat
  as probe noise / eventual consistency between consolidations; re-check if
  product needs strict read-your-writes on every upsert.
- Decision: **ship** opt-in; keep default `shared_mutex` without consolidate;
  use E3 when publish-swap is on and write rate matters more than immediate
  visibility of each upsert.

### 2026-07-24 — Phase E2 publish-swap concurrency spike

```text
Hypothesis: Opt-in publish-swap (copy→mutate→atomic publish) cuts mixed /search
            p99 vs legacy shared_mutex by removing unique_lock from readers.
Primary metric(s):   hey mixed /search p99 exact+typo (legacy vs --publish-swap)
Secondary metric(s): mixed writes/s; TSan; correctness; flag-off micro gate
Before: e2_mixed_legacy_20260724T132213Z.txt
After:  e2_mixed_swap_20260724T132456Z.txt
Correctness: ./scripts/run_correctness.sh — pass (incl. TSan publish-swap)
Micro gate:  flag-off vs baseline — pass
DoD items:   [x] flag off default  [x] flag on path  [x] probe  [x] TSan
Decision:    ship — opt-in only; do not flip default; E3 if write churn hurts
```

- Surfaces: `--publish-swap`, `HOUND_PUBLISH_SWAP=1`; `FuzzyBackend::clone()`,
  Trie/BkTree deep copy; `begin_bulk()` defers publish for CSV/JSON load;
  prepare-before-publish so readers never rebuild SymSpell deletes.
- Metrics (adjusted probe: search-only → mixed, writer `--exclude 1`):

  | scenario | query | legacy p99 | swap p99 | Δ |
  |----------|-------|------------|----------|---|
  | mixed | exact | 1178 ms | 1056 ms | **−122 ms** |
  | mixed | typo | 1149 ms | 1055 ms | **−94 ms** |
  | mixed | writes/s | 16.5 | 3.8 | −77% (prepare+clone) |

- Size/request stayed ~622 B (query doc preserved — unlike E1 empty-hit caveat).
- hey still shows ~1 s p99 floor (`-disable-keepalive`); treat deltas as coarse.
- Correctness: pass; micro flag-off: pass.
- Decision: **ship** opt-in; keep default `shared_mutex`; consider **E3** if
  product write rate needs >~4 upserts/s under concurrent search.

### 2026-07-23 — Phase E1 mixed-load concurrency probe

```text
Hypothesis: Mixed continuous upserts raise /search p99 vs search-only via
            unique_lock + HttpApi write mutex.
Primary metric(s):   hey /search p50/p95/p99 (exact + typo), search-only vs mixed
Secondary metric(s): writes/s write-only and mixed
Before: search-only (+ write-only) in benchmarks/results/e1_mixed_20260724T020340Z.txt
After:  mixed in same artifact
Correctness: N/A (probe only; no index/API change)
Micro gate:  N/A
DoD items:   [x] three scenarios  [x] numbers recorded  [x] scripts/tmp gitignored
Decision:    ship — E1 baseline recorded; next E2
```

- Commands: `./scripts/tmp/probe_e1_mixed_load.sh` (untracked; local only)
- Artifact: `benchmarks/results/e1_mixed_20260724T020340Z.txt` (`docs=5000`, `n=2000`, `c=50`, `duration_s=15` per scenario)
- Metrics:

  | scenario | query | p50 | p95 | p99 | writes/s |
  |----------|-------|-----|-----|-----|----------|
  | search-only | exact | 0.3 ms | 1005.2 ms | 1023.2 ms | — |
  | search-only | typo | 0.3 ms | 1010.6 ms | 1024.1 ms | — |
  | write-only | — | — | — | — | 870.3 |
  | mixed | exact | 0.5 ms | 1356.0 ms | 1667.5 ms | 6.5 |
  | mixed | typo | 0.4 ms | 459.7 ms | 2110.6 ms | 6.5 |

- Contention signal: mixed − search-only p99 (exact / typo) = **+644.3 ms** / **+1086.5 ms**
- Correctness: N/A
- Micro gate: N/A
- Decision: **ship** — unlocks E2 design spike
- Notes:
  - Mixed `Size/request` ~14 B vs search-only ~622 B (exact) — after write-only, rotating upserts rewrite texts; mixed search hit-set may be empty while still HTTP 200. Treat p99 delta as lock/scheduling contention signal, not identical query work.
  - Scenario order is search-only → write-only → mixed on one process (plan-mandated); E2 probes may want search-only vs mixed without intervening write-only, or writer that preserves searchable text.
  - Mixed typo p95 can be *lower* than search-only while p99 is higher (heavy tail).
  - No `--snapshot`; hey `-disable-keepalive`; probe stays under `scripts/tmp/`.

### 2026-07-23 — Close issue #1 (SymSpell delete-map compaction)

```text
Hypothesis: Two shipped slices meet #1 acceptance (RSS + prepare/Insert down,
            fuzzy gates hold); remaining ideas are optional future work.
Primary metric(s):   RSS @20k ~418→226 MB (−46%); BM_Insert/20000 much faster
Secondary metric(s): BM_SearchFuzzy/20000/{1,2} within gate; correctness green
Correctness: already green at last #1 slice
Micro gate:  pass vs versioned baseline (Insert faster than baseline)
Decision:    close #1 — no new GitHub issues for denser map / incremental deletes yet
```

- Shipped: `e669abd` (uint32 postings), `7e35c17` (tagged single/multi values)
- Rejected (measured): arena/`string_view` keys; split one/multi maps
- Roadmap-only leftovers: denser open-addressing map; incremental deletes on upsert
- Optional: human `save_baseline.sh` to refresh Insert gate numbers
- Decision: **close** [#1](https://github.com/carvalhosauro/hound/issues/1)

### 2026-07-23 — Issue #1 slice: tagged single/multi delete values

```text
Hypothesis: Most delete keys hit one word; store uint32 id directly in the map
            (MSB clear) and only allocate vector postings when a key collides
            (MSB set → index). One lookup; much less vector heap overhead.
Primary metric(s):   RSS @20k; BM_Insert/20000; BM_SearchFuzzy/20000/{1,2}
Secondary metric(s): prepare_ms; golden BK↔SymSpell parity
Before: uint32-vector slice — rss≈327 MB; Insert≈1456 ms; Fuzzy/2≈5.56 µs
After:  tagged map — rss≈226 MB; Insert≈834 ms; Fuzzy/2≈5.60 µs
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  vs baseline pass (Insert −69%, Fuzzy faster/ok)
DoD items:   [x] measured  [x] tagged values  [x] parity  [x] no fuzzy regress
Decision:    ship — then close #1 (acceptance met with uint32 slice)
```

- Rejected earlier this session: byte-arena + `string_view` keys — RSS −25 MB but
  `BM_SearchFuzzy/20000/2` missed the +10% gate (avg delete key ~14 B → SSO).
- Split one/multi maps: great RSS but two probes on miss → fuzzy regress; replaced
  by tagged single-map design.
- Metrics vs pre-#1 (~418 MB / slow Insert): RSS **−46%**, Insert much faster.
- Decision: **ship**; #1 closed after this + uint32 slice.

### 2026-07-23 — Issue #1 slice: uint32 delete postings

```text
Hypothesis: Storing dictionary word ids (uint32_t) in SymSpell delete posting
            lists instead of duplicated std::string cuts RSS and prepare cost
            without hurting fuzzy lookup.
Primary metric(s):   RSS delta @20k after prepare; BM_Insert/20000; prepare_ms
Secondary metric(s): BM_SearchFuzzy/20000/{1,2}; BK↔SymSpell golden parity
Before: probe rss_delta_mb≈418; prepare_ms≈1178; micro_iss1_before.json
After:  probe rss_delta_mb≈327; prepare_ms≈1042; micro_iss1_after.json
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  compare_bench vs baseline — pass (Insert/fuzzy improved or ok)
DoD items:   [x] measured  [x] uint32 postings  [x] parity  [x] suite green
Decision:    ship slice — continue #1 (tagged values next)
```

- Commands:
  - `scripts/tmp/probe_symspell_rss_*` (untracked local probe)
  - `hound_bench_micro --benchmark_filter='BM_Insert/20000|BM_SearchFuzzy/20000/…'`
  - `./scripts/compare_bench.py baselines/micro_baseline.json …/micro_iss1_after.json`
- Metrics:

  | metric | before | after | Δ |
  |--------|--------|-------|---|
  | RSS @20k (delta MB) | ~418 | ~327 | **−22%** |
  | prepare_ms (probe) | ~1178 | ~1042 | −12% |
  | `BM_Insert/20000` (same-host) | 1536 ms | 1456 ms | −5% |
  | `BM_SearchFuzzy/20000/2` | 6.40 µs | 5.56 µs | −13% |

- Correctness: pass (incl. golden BK↔SymSpell + TSan)
- Micro gate: pass (no `save_baseline.sh` — Insert already faster than versioned baseline)
- Decision: **ship** slice; proceed to tagged-values slice
- Notes: Delete **keys** are still `std::string` map keys — arena attempt later rejected (SSO).

### 2026-07-23 — Phase D closed (commits + follow-up issues)

```text
Hypothesis: Phase D DoD is met; remaining gaps are optional test hardening.
Primary metric(s):   D1–D3 checklist + commits on main
Secondary metric(s): GitHub issues #2–#5 filed for CLI/aliases/macro/concurrency
Correctness: already green at D3 ship
Micro gate:  N/A (docs/status only)
Decision:    ship — Phase D ✅; next E1 or #1 (then #1 closed same day)
```

- Commits: `515569e` (D1–D2 core), `65e8a46` (D3 API), `9e0a490` (docs)
- Follow-ups: [#2](https://github.com/carvalhosauro/hound/issues/2),
  [#3](https://github.com/carvalhosauro/hound/issues/3),
  [#4](https://github.com/carvalhosauro/hound/issues/4),
  [#5](https://github.com/carvalhosauro/hound/issues/5)
- Decision: **ship** — Phase D exit met

### 2026-07-23 — Phase D3 wire ranker through HTTP / CLI

```text
Hypothesis: Clients can select linear vs tie_break ranking without a custom
            binary; omitting ?ranker keeps today’s ScoreMerger default and
            JSON shape.
Primary metric(s):   HTTP integration order + 400 on invalid ranker
Secondary metric(s): default omit path == linear; docs in README
Before: N/A (new optional query/CLI surface)
After:  test_http_api [d3] + --ranker / HOUND_RANKER / ?ranker=
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  N/A (default rank path unchanged when ?ranker omitted)
DoD items:   [x] query param  [x] CLI/env  [x] docs  [x] no JSON break  [x] suite green
Decision:    ship — Phase D complete; next E1 or #1 (then #1 closed same day)
```

- Surfaces: `--ranker`, `HOUND_RANKER`, `GET /search?ranker=linear|tie_break`
- Aliases: `score_merger`→linear, `tiebreak`→tie_break
- Correctness: pass (integration locks equal-text order for both rankers)
- Micro gate: N/A
- Decision: **ship**

### 2026-07-23 — Phase D2 Typesense-style TieBreakRanker (opt-in)

```text
Hypothesis: Optional lexicographic ranker (text → external → id) gives
            Typesense-like tie-break ordering without changing default
            ScoreMerger blend behavior.
Primary metric(s):   ranking fixture order stability (unit)
Secondary metric(s): default FuzzyIndex order unchanged; correctness suite
Before: N/A (new optional path)
After:  test_tie_break_ranker fixtures + default still ScoreMerger
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  N/A (default search/rank path unchanged; no ScoreMerge edit)
DoD items:   [x] TieBreakRanker  [x] fixtures document order  [x] default unchanged
             [x] FuzzyIndex inject works  [x] suite green
Decision:    ship — proceed to D3 (HTTP wire) only if product needs it
```

- Order: `text_relevance` desc → `external_score` desc → `id` asc
- Opt-in: `make_tie_break_ranker()`; default `make_default_ranker()` still `ScoreMerger`
- Contrasts locked: equal-text fixture differs from `ScoreMerger(alpha=1)`;
  text-primary beats high-external (unlike `ScoreMerger(alpha=0)`)
- Correctness: pass (54 tests + TSan)
- Micro gate: N/A
- Decision: **ship**

### 2026-07-23 — Phase D1 Ranker interface + ScoreMerger default

```text
Hypothesis: Introduce Ranker so alternate rankers can plug in later without
            changing FuzzyIndex call sites; ScoreMerger remains default and
            preserves today’s linear α blend scores.
Primary metric(s):   BM_ScoreMerge/{64,256,1024}; score parity unit tests
Secondary metric(s): BM_SearchFuzzy/20000/{1,2}; BM_SearchExact/20000 (guards)
Before: baselines/micro_baseline.json (ScoreMerge + search gates)
After:  micro_d1_gate.json (quiet re-run of gate names)
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  compare_bench.py — pass (ScoreMerge ±4%; SearchFuzzy/Exact within gate)
DoD items:   [x] Ranker interface  [x] ScoreMerger implements  [x] FuzzyIndex wired
             [x] unit parity + inject test  [x] suite green  [x] micro gate
Decision:    ship — proceed to D2 (optional tie-break ranker)
```

- Commands:
  - `./scripts/run_correctness.sh`
  - `./build-bench/hound_bench_micro --benchmark_filter='BM_SearchFuzzy/20000/|BM_SearchExact/20000|BM_ScoreMerge' --benchmark_min_time=0.5s`
  - `./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_d1_gate.json`
- Metrics (cpu_time vs baseline, quiet gate re-run):

  | metric | Δ |
  |--------|---|
  | `BM_ScoreMerge/64` | +2.7% |
  | `BM_ScoreMerge/256` | −2.1% |
  | `BM_ScoreMerge/1024` | +3.4% |
  | `BM_SearchFuzzy/20000/1` | −1.3% |
  | `BM_SearchFuzzy/20000/2` | −5.6% |
  | `BM_SearchExact/20000` | +0.3% |

- Correctness: pass (incl. TSan)
- Micro gate: pass
- Decision: **ship**
- Notes: Full `run_micro.sh` under high load_avg showed false REGRESS noise on
  ScoreMerge/search; isolated gate re-run is authoritative for D1.

### 2026-07-23 — Accept SymSpell micro baseline

```text
Hypothesis: Human accepts SymSpell-default ingest/search tradeoff as the new
            versioned gate after documenting backend use cases.
Primary metric(s):   baselines/micro_baseline.json (SymSpell default micro)
Commands: ./scripts/run_micro.sh → save_baseline.sh micro_20260723T200401Z.json
Correctness: N/A (baseline promotion)
Micro gate:  new reference point for compare_bench.py
Decision:    ship — baseline updated; BK remains escape hatch
```

- Gate snapshot (cpu_time): Insert/20k ~2703 ms; SearchFuzzy/20k/1 ~1.93 µs;
  SearchFuzzy/20k/2 ~7.32 µs; SearchExact/20k ~0.91 µs
- Docs: README + AGENTS + REFINEMENT backend use-case tables
- Decision: **ship**

### 2026-07-23 — Phase C2 wire adaptive edit distance into search

```text
Hypothesis: Default searches use the C1 length→distance table; explicit
            max_edit_distance override preserves fixed-d tests/benches.
Primary metric(s):   unit adaptive behavior; golden with explicit d=2
Secondary metric(s): micro (benches pass explicit d — expect parity)
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  benches unchanged (explicit max_edit_distance)
DoD items:   [x] wired  [x] override  [x] docs  [x] suite green
Decision:    ship — Phase C complete
```

- `SearchOptions::max_edit_distance` is now `optional<int>` (nullopt = adaptive)
- HTTP: `?max_edit_distance=N` optional override
- Correctness: pass
- Micro gate: N/A delta expected (bench args still fixed)
- Decision: **ship**

### 2026-07-23 — Phase C1 adaptive edit-distance table

```text
Hypothesis: Document + lock a length→max_edit_distance table before wiring
            into FuzzyIndex (C2), so short queries cannot explode candidates.
Primary metric(s):   unit tests for adaptive_max_edit_distance / resolve_*
Secondary metric(s): N/A (spec only)
Correctness: unit + full suite green
Micro gate:  N/A (not wired into search yet)
DoD items:   [x] table documented  [x] unit tests  [x] changelog
Decision:    ship — proceed to C2 wiring
```

- Table: len≤2 → 0; 3–5 → 1; 6+ → 2 (`adaptive_edit_distance.hpp`)
- Correctness: pass
- Micro gate: N/A
- Decision: **ship**

### 2026-07-23 — Phase B5 demote BK to oracle/escape hatch

```text
Hypothesis: With SymSpell as default, BK should not live in the primary
            fuzzy_backend header / mental hot path; keep it as an explicit
            oracle + CLI/env escape hatch only.
Primary metric(s):   correctness (no perf claim)
Secondary metric(s): default still SymSpell; BK selectable for tests
Before/After:        N/A structure demotion
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  N/A (no scoring/search algorithm change)
DoD items:   [x] BK off hot-path header  [x] oracle kept  [x] suite green
Decision:    ship — Phase B complete; proceed to Phase C
```

- Changes: `BkFuzzyBackend` → `include/hound/bk_fuzzy_backend.hpp`;
  `fuzzy_backend.hpp` is interface + kind only; factory default arm is SymSpell.
- Correctness: pass
- Micro gate: N/A
- Decision: **ship**

### 2026-07-23 — Phase B4 SymSpell becomes default (insert regression justified)

```text
Hypothesis: After lazy delete-map + faster delete generation, SymSpell search
            wins hard enough to become default; ingest stays slower but OK for
            bulk-load-then-serve (amortized by query savings).
Primary metric(s):   BM_SearchFuzzy/20000/{1,2} vs BK and vs versioned baseline
Secondary metric(s): golden recall; BM_Insert/20000 (guard — expected regress)
Before: HOUND_FUZZY_BACKEND=bk → micro_b4_bk.json
After:  HOUND_FUZZY_BACKEND=symspell → micro_b4_sym.json (then flip compile default)
Correctness: ./scripts/run_correctness.sh — pass
Micro gate:  intentional Insert regression vs baselines/micro_baseline.json
DoD items:   [x] fuzzy improve  [x] recall≥baseline  [x] default flipped
             [x] save_baseline NOT run (needs human accept)
Decision:    ship — SymSpell default; keep BK via --fuzzy-backend bk
```

- Commands:
  - `HOUND_FUZZY_BACKEND=bk ./scripts/run_micro.sh benchmarks/results/micro_b4_bk.json`
  - `HOUND_FUZZY_BACKEND=symspell ./scripts/run_micro.sh benchmarks/results/micro_b4_sym.json`
  - `./scripts/run_correctness.sh`
- Metrics (cpu_time):

  | metric | BK | SymSpell | vs BK | vs baseline |
  |--------|-----|----------|-------|-------------|
  | `BM_SearchFuzzy/20000/1` | 155 µs | 2.61 µs | −98.3% | −97.6% |
  | `BM_SearchFuzzy/20000/2` | 1196 µs | 8.00 µs | −99.3% | −99.0% |
  | `BM_SearchExact/20000` | 6.20 µs | 1.46 µs | −76% | −57% |
  | `BM_Insert/20000` | 302 ms | 3322 ms | +1001% | +3118% |

- Correctness: pass (golden BK↔SymSpell hit ids; recall@k=1 on golden)
- Micro gate: **justified regression** on Insert; SearchFuzzy/Exact improve
- Decision: **ship** — flip default to SymSpell; **do not** `save_baseline.sh` yet
- Notes: `prepare()` / first search builds delete map; bulk paths call `prepare()`.
  Escape: `HOUND_FUZZY_BACKEND=bk`, `--fuzzy-backend bk`, `-DHOUND_DEFAULT_FUZZY_BACKEND_BK`.

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

## Phase H — Generic attrs + multi-index (product direction)

**Status:** **H1 attrs equality (slice 1) Done** — string attrs on flat `/search` /
`/index`; wire spec [`superpowers/specs/2026-07-27-attrs-equality-design.md`](superpowers/specs/2026-07-27-attrs-equality-design.md).
**Multi-index (slice 2) not started.** Orthogonal to E/F/G (perf / layout). Keep
core domain-agnostic: no business schemas (food, stores, etc.).

Hound stays a **sidecar**: RDBMS is source of truth; search returns ranked
`id`s; the app **hydrates** full records. Global quality lives in
`external_score` (computed by the app/job). Request-time signals (distance,
personalization) stay in the app as **rerank** after over-fetch — not in the
static index.

### H0 — Problem framing (locked intent)

| Concern | Mechanism | Not |
|---------|-----------|-----|
| Typo / text match | `text` + fuzzy backend | Business field names in core |
| “Good” entities first | `external_score` from app | Random or per-user score in index |
| Partition / eligibility | **attrs** (indexed metadata filters) | Encoding city into score |
| Distinct corpora | **multi-index** (collections) | One blob of unrelated verticals |
| Sync fan-in | External bus (Kafka/Rabbit) → HTTP upsert | Hound speaking Kafka |

**City / region edge case:** large markets dominate absolute volume scores.
Fix with `filter(attrs)` (e.g. `city_id`) so ranking runs *inside* the
eligible set — not by weakening global score. Optional later: per-region
`external_score` computed in the app before sync.

**Verticals (restaurants vs products vs neighborhoods):** prefer separate
**indexes/collections** early if schemas and churn diverge; a single
`vertical` attr is OK for a thin POC. Message-bus *topics* are an ingest
concern outside Hound; they map cleanly onto one consumer → one index.

### H1 — Attrs equality (slice 1) — Done

Locked wire: **string** attr values, **flat** routes (`GET /search`, `POST /index`).
Full contract: [`superpowers/specs/2026-07-27-attrs-equality-design.md`](superpowers/specs/2026-07-27-attrs-equality-design.md).

```json
{
  "id": "42",
  "text": "synthetic item alpha",
  "external_score": 0.81,
  "attrs": {
    "establishment_id": "17",
    "open": "1"
  }
}
```

```http
GET /search?q=alfa&limit=10&attrs.establishment_id=17&attrs.open=1
```

**Measured trade-offs** (Release bench, 20k docs, `tenant` attrs with 64 values;
filtered search `tenant="0"` ~1/64 eligible; gate legacy names within +10% vs
`baselines/micro_baseline.json`):

| Metric | Unfiltered / no attrs | With attrs / filtered |
|--------|----------------------|------------------------|
| `BM_Insert/20000` | **1172 ms** (gate) | — |
| `BM_InsertWithAttrs/20000` | — | **1147 ms** (~flat vs Insert) |
| `BM_SearchFuzzy/20000/1` | **1.72 µs** (gate) | — |
| `BM_SearchFuzzyFiltered/20000/1` | — | **11.5 µs** (~6.7×) |
| `BM_SearchFuzzy/20000/2` | **6.15 µs** (gate) | — |
| `BM_SearchFuzzyFiltered/20000/2` | — | **16.3 µs** (~2.6×) |

**Under-fetch fixture** (unit `test_attrs_filter.cpp`): 33 docs share `text`
`Shared Label`; only id `eligible` has `tenant=zz`. `limit=64` + filter → 1 hit;
`limit=1` + same filter may return **0** hits (gather ∩ eligible).

Detail + compare command: Phase 2 changelog **2026-07-30 — H1 attrs equality micro
trade-offs (Task 5)** above.

### H1 — Multi-index sketch (slice 2 — not shipped)

Illustrative target for a later MINOR/MAJOR slice (named indexes mirroring flat ops):

```http
GET /indexes/{name}/search?q=alfa&attrs.city_id=17&limit=50
```

POC without multi-index: one corpus + tenant attr filters (slice 1) or over-fetch
then filter in the RDBMS (H0 filter-after).

### H2 — Acceptance (attrs slice 1)

- [x] Attr equality filters are generic string keys/values; no domain types
- [ ] Multi-index: isolate docs by index name; search does not leak across
- [x] Default path still supports `{id,text,external_score}` with empty attrs
- [x] Synthetic examples + tests only; README documents hydrate-in-app pattern
- [x] Correctness suite green; micro gate for search path (+10% on legacy names)
- [x] Measured trade-offs published (insert-with-attrs, filtered fuzzy, under-fetch fixture)
- [x] OpenAPI + compat updated in the same change set as implementation

### Suggested slice order

1. **H0** (this section) — done as docs.
2. Filter-after POC in a consumer app (no Hound change).
3. ~~**H1** design spike~~ → **H1 attrs equality** shipped (slice 1).
4. **H1 slice 2** multi-index → optional numeric ranges / richer filters.

Do **not** block **F**/**H** product work on unfinished H slices. Prefer measuring
real bottlenecks before growing the document model, unless a POC needs in-process
attrs.

---

## Sync — keeping Hound aligned with the RDBMS

**Status:** design + **shipped DX recipes**. Hound does **not** pull from the DB by
itself; the app/job **pushes** thin docs. Guides:
[`sync-reload.md`](sync-reload.md) (**A**),
[`sync-writethrough.md`](sync-writethrough.md) (**B**),
[`snapshot.md`](snapshot.md), index in [`DX.md`](DX.md).
Product framing also in Phase H0 (sync fan-in).

### Contract

| Side | Owns | Mechanism |
|------|------|-----------|
| RDBMS | Source of truth (full rows, business rules) | SQL / app writes |
| App / job | Projection `{ id, text, external_score }` (+ future attrs) | Push into Hound |
| Hound | Ranked candidate ids | `POST /index`, `/index/bulk`, `DELETE /index/{id}`, `--load` CSV/JSON |

Idempotent upsert by `id`. Deletes must be explicit (or rebuild from a full export).

### Patterns (pick by churn)

| Pattern | How | Best when | Lag | Cost |
|---------|-----|-----------|-----|------|
| **A. Full reload** | Periodic `SELECT` → CSV/JSON → `--load` / bulk (+ optional `--snapshot`) | Small corpora; rare writes; simplest ops | Minutes–hours | Rebuild RAM index each run |
| **B. Write-through** | App (or outbox worker) `POST /index` on create/update; `DELETE` on remove | Interactive UIs; near-real-time suggest | Seconds (or ~N ms with E3 consolidate) | Live upsert cost; SymSpell `prepare` unless E3 |
| **C. Outbox / CDC** | DB outbox or Debezium/etc. → worker → Hound HTTP | Many writers; reliable delivery | Seconds | Infra outside Hound |
| **D. Message bus** | Kafka/Rabbit topic → consumer → upsert | Already have a bus; multi-service fan-in | Seconds | Hound never speaks Kafka |

**Recommended default for “front door” apps:** **B** (write-through) for entities
users type against, plus occasional **A** rebuild as a safety net (e.g. nightly)
if drift is unacceptable.

### Concurrency knobs that affect sync

| Load shape | Suggested flags |
|------------|-----------------|
| Bulk load then mostly reads | Defaults (SymSpell + `shared_mutex`) |
| Mixed search + frequent upserts, care about search p99 | `--publish-swap --consolidate-ms N` (E3) |
| Tight RAM / rare search | `--fuzzy-backend bk` |

Snapshot (`--snapshot`) persists the **published** view across restarts; with E3
it can lag live upserts until consolidate/`prepare()` (see README).

### Non-goals

- Hound querying Postgres/MySQL directly
- Built-in CDC/Kafka clients
- Two-way sync (Hound → DB)

Those stay in the app or a thin worker — keeps the sidecar replaceable when
graduating to Meili/ES (same push shape: id + searchable text + sort field).

### Suggested sync slices

1. ~~Document recipes **A** and **B** under DX~~ — **done** (`sync-*.md` + demos).
2. CDC/bus templates only if a real consumer needs them (still outside Hound).
3. Optional: more synthetic workers — only when copy-paste gaps appear.

---

## Future decisions

1. **Concurrency defaults:** stay on legacy `shared_mutex`; publish-swap and
   consolidate remain opt-in (`--publish-swap`, `--consolidate-ms`) until a
   product workload justifies flipping defaults.
2. **Product direction:** **Phase H** (attrs + multi-index) when a consumer
   needs in-index filters; until then filter-after in the app is enough.
   Sync stays push-based (see **Sync** section + [`DX.md`](DX.md) D2).
3. **SymSpell further compaction** (denser map / incremental deletes): only if RSS
   or write-churn still hurts — file a new issue when starting that slice.
4. **Ranker test hardening (optional):** [#2](https://github.com/carvalhosauro/hound/issues/2)–[#5](https://github.com/carvalhosauro/hound/issues/5).
5. Revisit ART / contiguous layout only after a post-SymSpell profile (**F0→F1**).
6. On-disk compressed postings if snapshot rebuild/RSS hurts (**F2**).
7. Optional `fields=id` projection without removing score fields (**G1**).
8. **DX / SDKs:** OpenAPI shipped ([`openapi.yaml`](openapi.yaml) +
   [`compat.md`](compat.md)); thin hand-written client (**D5.2**) only on demand —
   codegen only after the surface grows.
9. **Product maturity (not popularity):** [`DX.md`](DX.md) **D7** — **D7.1–D7.4**
   + **D7.6** Done; remaining: dogfood write-up (**D7.5**).