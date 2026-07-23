# Hound — post-MVP refinement

Living document: baseline (Phase 0), prioritization (Phase 1), and changelog
with before/after numbers (Phase 2). **Feature refinements (SymSpell, etc.)
are not started yet** — Phase A of the testing suite added `shared_mutex` to
`FuzzyIndex` so concurrent search+upsert is defined under TSan.

Learning sources: Sonic, Typesense, Xapian, SymSpell, CppCon 2024
“When Nanoseconds Matter” (David Gross).

---

## Phase 0 — Baseline (2026-07-23)

Command (Release, existing synthetic dataset, fixed seed):

```bash
./build/hound_bench
```

| size | ingest_ms | p50_us | p95_us | p99_us | recall@10 | rss_mb |
|------|-----------|--------|--------|--------|-----------|--------|
| 1 000 | 2.75 | 63.1 | 91.6 | 108.1 | 1.00 | 6.0 |
| 5 000 | 20.0 | 252.4 | 432.1 | 510.5 | 1.00 | 12.1 |
| 20 000 | 99.3 | 1026.6 | 1965.7 | 2331.6 | 1.00 | 33.7 |

Notes:

- Recall@10 = 1.0 on the happy path (unique texts + distance-1 typos).
- Latency grows with N; at 20k, p50 is already ~1 ms.
- This is the mandatory **“before”** for any Phase-2 performance change.
- No `perf` profile yet — that remains priority P0 before heavy structure work.

### Current architecture (audited)

| Piece | Implementation | File |
|-------|----------------|------|
| Prefix | Trie with per-node `unique_ptr` + `unordered_map<char,…>` | `include/hound/trie.hpp` |
| Fuzzy | BK-tree + full `levenshtein()` per edge/query | `include/hound/bk_tree.hpp` |
| Orchestration | Sync upsert into Trie + BK + doc map | `include/hound/fuzzy_index.hpp` |
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
| Symmetric delete | **Not applied — #1 perf priority** | Fuzzy = BK-tree + repeated Levenshtein |
| Compound / word split | **Not applied** | Normalizer collapses spaces only |
| Practical distance ~2–3 | **Aligned** | Default max distance 2 |

### 5. “When Nanoseconds Matter”

| Learning | Status | Evidence |
|----------|--------|----------|
| Measure before optimizing | **Partial** | `hound_bench` exists; need `perf` |
| Cache-friendly layout | **Not applied** | Pointer-chasing nodes |
| Updates must not block searches | **Improved** | `shared_mutex` allows concurrent readers; writers still exclusive (no double-buffer / Sonic queue yet) |

---

## Phase 1 — Prioritization (effort × gain)

| # | Change | Effort | Expected gain |
|---|--------|--------|---------------|
| **P0** | `perf` profile of `search` at 20k | Low | Guides the rest |
| **P1** | SymSpell / symmetric delete | Medium | High latency/scalability |
| **P2** | Adaptive edit distance by term length | Low | Medium quality |
| **P3** | Pluggable `Ranker` interface | Medium | Extensibility |
| **P4** | Typesense-style tie-break ranker | Medium | Ranking quality |
| **P5** | Double-buffer / non-blocking writers (beyond shared_mutex) | Medium | Mixed load |
| **P6** | Background consolidation (Sonic-style) | High | Write churn |
| **P7** | SymSpell compound splitting | Medium | Case-by-case |
| **P8** | Contiguous layout / ART only if profile demands | High | Low–medium at this N |
| **P9** | On-disk index (future) | High | Persistence/RSS |

### Regression gate

Paste full `./build/hound_bench` before/after; justify or revert any p50/p95/p99 or recall@10 regression.

---

## Phase 2 — Changelog

_(No SymSpell/ranking refinement entries yet.)_

Note: testing-suite Phase A is tracked in [TESTING-AND-BENCHMARKS.md](TESTING-AND-BENCHMARKS.md).

---

## Future decisions

1. On-disk compressed postings if snapshot rebuild/RSS hurts.
2. Revisit ART only if trie dominates after SymSpell.
3. Optional `fields=id` projection without removing score fields.
