# E1 — Mixed-load concurrency probe (design)

Phase E slice **E1** only. Measurement delivery; no core/API lock changes.

Status: approved for implementation plan (2026-07-23).

## Goal

Record baseline contention numbers: how much `/search` latency degrades when
continuous HTTP upserts run alongside search load (Sonic-style “writers must
not stall readers” problem, measured before any double-buffer work in E2).

## Hypothesis

Under mixed load, exclusive writers (`FuzzyIndex` `unique_lock` + `HttpApi`
write mutex around upsert + optional snapshot) raise `/search` p99 (and likely
p95) versus an otherwise identical search-only run on the same host and
corpus.

## Scope

### In

- Untracked probe script: `scripts/tmp/probe_e1_mixed_load.sh`
- Background writer helper (inline Python or small companion under
  `scripts/tmp/`) for continuous `POST /index` with rotating document IDs
- Three sequential scenarios on one live Release `hound` process
- Artifact under `benchmarks/results/e1_mixed_<stamp>.txt` (gitignored)
- Changelog entry in `docs/REFINEMENT.md` (Phase 2 + status: E1 baseline
  recorded; next = E2)

### Out

- Double-buffer / publish-swap (E2)
- Background consolidation (E3)
- Promoting the probe into tracked `benchmarks/macro/`
- Micro benches / `compare_bench.py` / `save_baseline.sh`
- Changes to `FuzzyIndex`, `HttpApi`, or public API

## Approach

**Bash + hey + Python writer** (reuses macro bootstrap).

Rationale: search percentiles stay in the same dialect as
`benchmarks/macro/run_macro.sh`; rotating upsert bodies need a custom writer
(`hey` POST is fixed-body).

Rejected alternatives:

- All-Python HTTP load: numbers not comparable to macro `hey`
- Dual `hey` for writes: cannot rotate upsert IDs/bodies realistically

## Architecture

```text
probe_e1_mixed_load.sh
  ├─ corpus (seed 42, DOCS docs) — same generator style as run_macro.sh
  ├─ cmake Release build-bench → hound
  ├─ start hound --load corpus (ephemeral port)
  ├─ wait /health
  ├─ scenario search-only: hey /search exact + typo
  ├─ scenario write-only:  Python writer for DURATION seconds
  ├─ scenario mixed:       writer background + hey /search exact + typo
  └─ tee summary → benchmarks/results/e1_mixed_<stamp>.txt
```

### Scenarios

| Scenario | Search | Writes |
|----------|--------|--------|
| search-only | `hey` on `/search` (exact + typo queries, same encoding as macro) | none |
| write-only | none | continuous `POST /index`, IDs `doc-{i % DOCS}`, refreshed text/score |
| mixed | same `hey` N/C as search-only | writer runs for the duration of the `hey` runs |

### Writer behavior

- HTTP `POST /index` with JSON `{"id","text","external_score"}`
- Rotate through existing corpus IDs so upserts hit the update path (erase +
  reinsert under unique lock), maximizing lock hold time for E1 worst-case
- Report: total writes, wall seconds, writes/s
- On mixed: start before `hey`, stop after both search hey runs finish;
  ignore writer HTTP errors only if process killed at stop (log failures
  otherwise)

### Defaults (overridable via env)

| Variable | Default | Role |
|----------|---------|------|
| `HOUND_MACRO_N` | `2000` | hey request count |
| `HOUND_MACRO_C` | `50` | hey concurrency |
| `HOUND_MACRO_DOCS` | `5000` | corpus size / ID rotation modulus |
| `HOUND_E1_DURATION` | `15` | write-only wall-clock seconds (mixed lasts as long as hey runs) |
| `HOUND_HEY_EXTRA` | `-disable-keepalive` | same caveat as macro README |
| `HOUND_MACRO_PORT` | ephemeral | bind port |
| `HOUND_BENCH_BUILD_DIR` | `build-bench` | Release binary dir |

## Metrics

**Primary:** `/search` p50 / p95 / p99 from hey — search-only vs mixed (exact and
typo). Contention signal = mixed − search-only on p99 (and p95).

**Secondary:** writes/s on write-only and mixed; note if mixed write rate drops
under search load.

**Interpretation caveats:** hey includes loopback + HTTP + JSON + scheduling;
mixed adds a second client process. Treat deltas as coarse contention evidence
for E2, not core µs.

## DoD checklist

- [ ] Hypothesis written (above + changelog)
- [ ] Before metrics: search-only (+ write-only secondary) captured
- [ ] After metrics: mixed captured the same way
- [ ] Correctness: N/A for probe-only; no index/API code change
- [ ] Micro gate: N/A
- [ ] Phase 2 changelog entry filled (commands + table + decision)
- [ ] Temporary probe remains untracked under `scripts/tmp/` (add `scripts/tmp/`
      to `.gitignore` in the same docs commit so it cannot be committed by
      accident; keep the script locally until E2 reuses it)

**Decision expected:** `ship` — baseline contention recorded; unlocks E2 design.

## Changelog template (to paste into REFINEMENT.md)

```text
Hypothesis: Mixed continuous upserts raise /search p99 vs search-only via
            unique_lock + HttpApi write mutex.
Primary metric(s):   hey /search p50/p95/p99 (exact + typo), search-only vs mixed
Secondary metric(s): writes/s write-only and mixed
Before: search-only (+ write-only) in e1_mixed_<stamp>.txt
After:  mixed in same artifact
Correctness: N/A (probe only)
Micro gate:  N/A
DoD items:   [x] three scenarios  [x] numbers in changelog  [x] script tmp
Decision:    ship — E1 baseline recorded; next E2
```

## Risks

- Machine load variance between sequential scenarios — mitigate by running all
  three in one script invocation without idle gaps longer than needed
- Snapshot path: probe should run **without** `--snapshot` so write path is
  upsert-only (matches “API mutex around upsert”; avoids disk I/O dominating)
- Writer too slow to contend — if writes/s is tiny vs search RPS, note in
  changelog and optionally bump writer concurrency in a follow-up probe (still
  E1 iterate, not E2)
