# E3 — Background consolidation (design)

Phase E slice **E3**. Sonic-style time-based batched publish on top of E2
publish-swap. Default `shared_mutex` and E2 sync publish-swap (`consolidate_ms
== 0`) stay unchanged.

Status: approved for implementation plan (2026-07-24 / session).

**Depends on:** E2 publish-swap (`docs/superpowers/specs/2026-07-24-e2-publish-swap-design.md`,
changelog in `docs/REFINEMENT.md`).

## Goal

E2 opt-in publish-swap improved mixed `/search` p99 but collapsed mixed write
throughput (~16.5 → 3.8 writes/s) because every upsert deep-copies and runs
SymSpell `prepare` before publish. E3 amortizes that cost: writers mutate a
draft and mark dirty; a background worker consolidates at a configured interval
when dirty. Search still reads an atomic published snapshot (eventual lag).

## Hypothesis

Under mixed load with `--publish-swap --consolidate-ms N`, writes/s rise vs E2
sync publish-swap (same host, adjusted E2 probe) while mixed `/search` p99 stays
comparable to E2 (within hey noise / keepalive floor). Explicit `prepare()`
remains a sync flush.

## Decisions locked in brainstorming

| Topic | Choice |
|-------|--------|
| Visibility model | Time-based background consolidation (Sonic-like) |
| Relation to E2 | E3 only on top of publish-swap; legacy untouched |
| Reader freshness | Eventual; plus explicit `prepare()` flush |
| Interval default | None — require `--consolidate-ms N` / `HOUND_CONSOLIDATE_MS`; omit or `0` = E2 sync |
| Mechanism | `jthread` inside `FuzzyIndex` (not process-only timer; not lazy-on-next-write) |
| `clear` | Immediate publish (do not leave empty index delayed) |
| HTTP flush endpoint | Out of scope (use `prepare()` from process/tests/bulk) |
| Default concurrency | Unchanged (`shared_mutex`); do not flip publish-swap default |

## Scope

### In

- `FuzzyIndex` ctor: `PublishMode` + `std::chrono::milliseconds consolidate_ms = 0`
- When `PublishSwap` and `consolidate_ms > 0`: dirty draft; background worker
  publishes at most every N ms if dirty
- Wire `--consolidate-ms N` / `HOUND_CONSOLIDATE_MS`; require publish-swap
- Reuse `prepare()` as sync flush; `begin_bulk()` continues to defer until `prepare()`
- Unit + TSan coverage for delayed visibility, flush, dtor join, concurrent R/W
- Adjusted E2 probe (tmp): compare E2 vs E3 writes/s + mixed search p99
- `docs/REFINEMENT.md` Phase 2 changelog + status

### Out

- Count-based / idle-only triggers
- Consolidation on legacy `shared_mutex`
- Structural sharing / COW inside SymSpell
- Flipping default concurrency mode
- Public JSON shape changes
- New HTTP `/flush` endpoint
- Promoting probe to tracked `benchmarks/macro/` (optional later)

## Architecture

```text
FuzzyIndex
  mode: legacy | publish_swap
  consolidate_ms_                 # 0 → E2 sync publish each mutation

  legacy:
    mu_ (shared_mutex)            # unchanged

  publish_swap + consolidate_ms == 0:
    E2 behavior                   # prepare + publish under writer_mu_ each write

  publish_swap + consolidate_ms > 0:
    writer_mu_
    draft_                        # mutated by upsert/erase; clear publishes now
    dirty_                        # set when draft diverges from published
    defer_publish_                # begin_bulk(); worker skips until prepare()
    published_                    # atomic shared_ptr; search/get/size/copy read this
    worker_ (jthread)             # sleep ≤ N ms; if dirty && !defer → prepare+publish
    stop / condition_variable     # wake on prepare(), shutdown, optional dirty

  upsert / erase:
    lock writer_mu_
    mutate draft_
    if consolidate_ms_ == 0 && !defer_publish_: prepare + publish   # E2
    else: dirty_ = true                                             # E3
  clear:
    lock writer_mu_
    clear draft_
    prepare + publish immediately   # freshness for empty index
  prepare():
    lock writer_mu_
    draft_.fuzzy->prepare()
    published_.store(copy of draft_)
    dirty_ = false; defer_publish_ = false
    wake worker
  begin_bulk():
    defer_publish_ = true           # as E2; worker must not publish mid-bulk
  search / get / size / copy_documents:
    load published_                 # eventual; never draft_
```

### Flag surface

| Surface | How | Default |
|---------|-----|---------|
| Process | `--publish-swap` + `--consolidate-ms N` | no E3 |
| Env | `HOUND_PUBLISH_SWAP=1` + `HOUND_CONSOLIDATE_MS=N` | no E3 |
| Ctor | `FuzzyIndex(..., PublishMode::PublishSwap, consolidate_ms)` | `0` ms |

Rules:

- `N` is unsigned milliseconds. `N == 0` means E2 sync publish-swap.
- `--consolidate-ms` / non-zero env **without** publish-swap → CLI error + usage
  (do not silently ignore).
- Invalid / non-numeric `N` → CLI error + usage (follow existing CLI patterns).

### HttpApi

- Keep HTTP contracts identical.
- No new flush route; bulk load already ends with `prepare()`.
- Snapshot / `maybe_save` unchanged relative to E2.

## Metrics & DoD

**Primary:** mixed-load writes/s — E2 (`--publish-swap`) vs E3
(`--publish-swap --consolidate-ms N`) on adjusted E2 probe (search→mixed,
preserve query doc).

**Secondary:** mixed `/search` p50/p95/p99 exact+typo (must not collapse vs E2);
documented `N`; TSan; correctness suite; micro gate on default/legacy path.

**Done when:**

- [ ] Hypothesis + before/after in Phase 2 changelog (why E2 write cost needed E3)
- [ ] Flag off / legacy: correctness green; micro gate vs baseline
- [ ] E2 path (`publish-swap`, `consolidate_ms=0`): existing tests still green
- [ ] E3 path: unit (delayed visibility + `prepare()` flush + clean dtor);
      TSan concurrent search+upsert
- [ ] CLI rejects `--consolidate-ms` without `--publish-swap`
- [ ] Probe artifact under `scripts/tmp/` / `benchmarks/results/` (untracked)
- [ ] Decision: ship E3 opt-in / iterate / revert; **do not** flip defaults

Suggested probe `N` for first run: **200–500 ms** (tune if still prepare-bound).

## Risks

| Risk | Mitigation |
|------|------------|
| Stale search until tick | Document lag ≤ ~N + prepare; `prepare()` flush for tests/bulk |
| Worker + `writer_mu_` races | TSan test; join worker in dtor before destroying state |
| Interval too short → still prepare-bound | Probe with larger N; changelog records chosen N |
| `begin_bulk` races with worker | Worker skips while `defer_publish_`; `prepare()` ends defer |
| Readers hold old `shared_ptr` → transient RSS | Accept (same as E2); note in changelog |

## Changelog template (REFINEMENT.md)

```text
Hypothesis: Time-based consolidate under publish-swap raises mixed writes/s vs
            E2 sync publish without collapsing mixed /search p99.
Primary metric(s):   mixed writes/s (E2 vs E3); consolidate interval N
Secondary metric(s): mixed /search p99 exact+typo; TSan; correctness
Before: E2 publish-swap probe artifact
After:  E3 --consolidate-ms N probe artifact
Correctness: ./scripts/run_correctness.sh — pass (incl. TSan E3)
Micro gate:  legacy/default vs baseline — pass
DoD items:   [ ] …
Decision:    ship | iterate | revert
```
