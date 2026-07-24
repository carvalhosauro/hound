# E2 — Publish-swap concurrency spike (design)

Phase E slice **E2** only. Minimal double-buffer / publish-swap behind a flag;
default `shared_mutex` path unchanged. E3 (background consolidation) out of scope
unless E2 metrics fail the DoD.

Status: approved for implementation plan (2026-07-23 / session).

**Depends on:** E1 baseline (`docs/superpowers/specs/2026-07-23-e1-mixed-load-probe-design.md`,
changelog in `docs/REFINEMENT.md`).

## Goal

Writers must not stall readers under mixed HTTP load. Prove that an opt-in
publish-swap path improves `/search` p99 vs the E1 `shared_mutex` baseline,
with TSan clean and correctness green.

## Hypothesis

Under mixed load, exclusive `unique_lock` on `FuzzyIndex` (plus API-level write
mutex) elevates `/search` p99 (E1: exact **+644 ms**, typo **+1087 ms** vs
search-only). A publish-swap mode where readers `atomic_load` a
`shared_ptr<const IndexState>` and writers copy → mutate → `atomic_store`
removes reader/writer lock contention on the search hot path, cutting mixed
search p99 vs E1 (same host, adjusted probe).

## Decisions locked in brainstorming

| Topic | Choice |
|-------|--------|
| Scope this session | E2 spike only (not E3) |
| Mechanism | Classic publish-swap (deep-copy per mutation + atomic publish) |
| What is published | Entire index state: docs + trie + fuzzy backend |
| Placement | Inside `FuzzyIndex` (not HttpApi-only dual index) |
| Default path | Unchanged `shared_mutex` |
| Flag | Opt-in: `--publish-swap` / `HOUND_PUBLISH_SWAP=1` (default off) |
| Rejected for E2 | Periodic queue+batch publish (closer to E3; deferred) |
| Rejected | Wrapper type / dual `FuzzyIndex` at HTTP layer |

## Scope

### In

- Internal `IndexState` (or equivalent) holding `docs`, `Trie`, `FuzzyBackend`
- `FuzzyBackend::clone()` (or equivalent) on SymSpell + BK adapters
- `Trie` deep copy (today: `unique_ptr` root — not copyable; add explicit copy)
- `FuzzyIndex` dual mode: legacy mutex vs publish-swap
- CLI/env wire for the flag; README one-liner
- Unit + TSan coverage for flag-on visibility and concurrent search
- Adjusted E1 probe (tmp): search-only → mixed **without** intervening write-only
  that rewrites query texts; compare flag off vs on
- `docs/REFINEMENT.md` Phase 2 changelog + status (E2 done / E3 decision)

### Out

- Sonic-style background consolidation interval (E3)
- Structural sharing / COW inside SymSpell delete map
- Changing default concurrency mode
- Public JSON shape changes
- Promoting probe to tracked `benchmarks/macro/` (optional later)

## Architecture

```text
FuzzyIndex
  ranker_                         # outside published state (shared)
  mode: legacy | publish_swap

  legacy:
    mu_ (shared_mutex)
    docs_ / trie_ / fuzzy_        # as today

  publish_swap:
    writer_mu_                    # serializes mutations only
    atomic<shared_ptr<const IndexState>> published_
    upsert/erase/clear/prepare:
      lock writer_mu_
      auto next = copy(*published_)
      mutate(*next)
      published_.store(next)
    search/get/size/copy_documents:
      auto snap = published_.load()
      read *snap  # no shared_mutex
```

### Flag surface

| Surface | How | Default |
|---------|-----|---------|
| Process | `--publish-swap` or `HOUND_PUBLISH_SWAP=1` | off (legacy) |
| Ctor | `FuzzyIndex(..., PublishMode)` or bool | legacy |

Invalid / unknown: follow existing CLI patterns (error + usage).

### HttpApi

- Keep HTTP contracts identical.
- Spike minimum: avoid holding `HttpApi::mu_` across `index_.upsert` longer than
  needed when publish-swap already serializes writers; `maybe_save` may remain
  under a short API lock. No snapshot path required for the probe (same as E1).

### Probe adjustments (vs E1)

E1 ran write-only **before** mixed, rotating upserts that rewrote corpus texts;
mixed `/search` then saw ~14 B responses (likely empty hits). For E2:

1. Order: **search-only → mixed** (optional write-only **after**, or separate run).
2. Writer must **not** destroy the hey query document’s searchable text
   (e.g. rotate IDs excluding `doc-0`, or upsert additive fields only on other ids).
3. Run twice: legacy (flag off) and publish-swap (flag on); same N/C/DOCS.
4. Primary metric: mixed `/search` p99 (exact + typo) flag-on vs E1 / flag-off.
5. Secondary: writes/s under mixed (expect drop under flag-on due to deep-copy).

## Copy / clone requirements

| Component | Need |
|-----------|------|
| `std::unordered_map` docs | natural copy |
| `Trie` | explicit deep copy of node tree |
| `FuzzyBackend` | new `virtual std::unique_ptr<FuzzyBackend> clone() const = 0` |
| `SymSpellBackend` / `BkFuzzyBackend` | implement `clone()` (copy dictionary + delete map / BK tree) |
| `Ranker` | **not** cloned per publish; stays on `FuzzyIndex` |

`prepare()`: after mutating a copied SymSpell state, call `prepare()` on the
shadow if the backend requires it before publish (or ensure copy includes a
ready delete map). Document the chosen rule in the impl plan.

## Metrics & DoD

**Primary:** hey mixed `/search` p50/p95/p99 — publish-swap vs legacy (and vs E1
recorded numbers on same host if comparable).

**Secondary:** mixed writes/s; TSan; correctness suite; micro gate on **flag-off**
only (insert regression under flag-on is **justified** in changelog).

**Done when:**

- [ ] Hypothesis + before/after in Phase 2 changelog
- [ ] Flag off: correctness green; micro gate vs baseline (no unexplained >10%)
- [ ] Flag on: correctness green; TSan clean on concurrent search+upsert
- [ ] Probe: mixed search p99 improves vs E1/legacy under comparable load
- [ ] Temporary probe stays under `scripts/tmp/` (already gitignored)
- [ ] Decision: ship E2 / iterate / open E3 with numbers

If flag-on does **not** improve p99 (or writes/s collapses so mixed load is
unrealistic): changelog documents why; consider E3 (batched publish) as next —
do not silently flip default.

## Risks

| Risk | Mitigation |
|------|------------|
| SymSpell deep-copy too slow → writer starves | Measure writes/s; shrink DOCS in probe or accept and pivot design toward E3 batching |
| Readers hold old `shared_ptr` → transient RSS | Accept for spike; note in changelog |
| Copy bugs → missing hits | Golden/unit parity flag off vs on on same fixture |
| E1-style empty mixed results | Probe order + writer preserve query docs |

## Changelog template (REFINEMENT.md)

```text
Hypothesis: Publish-swap (flag on) cuts mixed /search p99 vs E1 shared_mutex
            by removing unique_lock from readers.
Primary metric(s):   hey mixed /search p99 exact+typo (legacy vs publish-swap)
Secondary metric(s): mixed writes/s; TSan; correctness
Before: E1 / flag-off probe artifact
After:  flag-on probe artifact
Correctness: ./scripts/run_correctness.sh — pass (both modes as applicable)
Micro gate:  flag-off vs baseline; flag-on Insert justified if regresses
DoD items:   [ ] …
Decision:    ship | iterate | E3
```
