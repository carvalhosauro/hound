# Search params & process flags

One-page reference for `GET /search` query params and process knobs that change
ranking, fuzzy cost, or concurrency. Roadmap id **D4.2**.

Wire contract: [`openapi.yaml`](openapi.yaml). Stability rules: [`compat.md`](compat.md).

Happy path stays: omit most knobs. Defaults are fine for autocomplete + hydrate.

---

## `GET /search` query params

```bash
curl -sS 'http://127.0.0.1:8080/search?q=ada%20ash&limit=5&alpha=0.7'
curl -sS 'http://127.0.0.1:8080/search?q=ada&max_edit_distance=1&ranker=tie_break'
curl -sS 'http://127.0.0.1:8080/search?q=shared&limit=10&attrs.tenant=zz'
```

| Param | Required | Default | Meaning |
|-------|----------|---------|---------|
| `q` | **yes** | — | Query string (normalized internally) |
| `limit` | no | `10` | Max hits returned |
| `alpha` | no | `0.7` | Weight on text relevance vs normalized `external_score` when `ranker=linear` |
| `max_edit_distance` | no | **adaptive** (see below) | Cap on Levenshtein-style edits for fuzzy |
| `ranker` | no | process default (`linear` unless `--ranker` / `HOUND_RANKER`) | `linear` or `tie_break` |
| `attrs.<key>` | no | — | Equality filter on indexed attrs (string value). Repeat per key; **AND** across keys. Omit all `attrs.*` → no attr filter (v0.1.0 behavior). Doc missing a filter key → does not match that filter. |

### Adaptive `max_edit_distance` (when omitted)

| Normalized query length | Distance |
|-------------------------|----------|
| 0–2 | 0 (exact / prefix only) |
| 3–5 | 1 |
| 6+ | 2 |

### Use case & trade-off (per query)

| Param | Use when | Trade-off |
|-------|----------|-----------|
| `q` | Every search | Empty/`q` missing → error |
| `limit` | UI page size; over-fetch before app-side filters | Higher `limit` = more merge/sort work; hydrate more rows |
| `alpha` | Tune “typo match” vs “business score” (`linear` only) | High α → quality/score ignored; low α → weak text matches can rank high on score alone. **Ignored** by `tie_break` |
| `max_edit_distance` | Force stricter (`0`/`1`) or looser (`2`) than adaptive | Higher distance → more candidates, slower fuzzy, noisier suggest — especially on short tokens |
| `ranker=linear` | Blend text + `external_score` with α | Need to pick α; ties broken by score blend, not a strict priority chain |
| `ranker=tie_break` | Typesense-style: text first, then external, then id | Ignores `alpha`; `score` in JSON is text relevance (primary key), not the blend |
| `attrs.<key>` | Restrict search to an eligible set (e.g. tenant `establishment_id`) before rank + `limit` | Text gather runs first; results are intersected with attr postings. Crowded shared labels across tenants + tight `limit` can return **fewer** hits than `limit` (under-fetch). Mitigation: raise `limit`; see measured cost in [`REFINEMENT.md`](REFINEMENT.md) (2026-07-30 H1 micro entry) |

**Response fields (unchanged):** `id`, `score`, `text_relevance`, `external_score` (no `attrs` in hits — hydrate in app).

### Attr filters (AND equality)

- Values on the wire are **strings** (serialize numeric ids as `"17"`).
- Filters combine with **AND**. Empty string values are valid.
- Ranking runs on eligible candidates only, then `limit` is applied.
- **Under-fetch:** if fuzzy/prefix gather returns mostly ids outside the eligible
  set, you may get an empty or short page even when eligible docs exist. Unit
  fixture: 33 docs share `text`, one eligible tenant `zz` — `limit=64` returns
  that id; `limit=1` may return **0** hits. Trade-offs and µs numbers:
  [`REFINEMENT.md`](REFINEMENT.md) § Phase 2 changelog (2026-07-30).

---

## Process flags (CLI / env)

Set at process start. Per-query `?ranker=` still overrides the process ranker for
one request.

| Flag | Env | Default | Meaning |
|------|-----|---------|---------|
| `--host` | — | `127.0.0.1` | Bind address (`0.0.0.0` in Docker images) |
| `--port` | — | `8080` | Listen port |
| `--load FILE` | — | none | Bulk CSV/JSON before serve |
| `--snapshot PATH` | — | none | Load/save binary snapshot — [`snapshot.md`](snapshot.md) |
| `--fuzzy-backend bk\|symspell` | `HOUND_FUZZY_BACKEND` | `symspell` | Fuzzy dictionary implementation |
| `--ranker linear\|tie_break` | `HOUND_RANKER` | `linear` | Default ranker for searches without `?ranker=` |
| `--publish-swap` | `HOUND_PUBLISH_SWAP=1` | off (`shared_mutex`) | Readers use published snapshot; writers don’t block reads |
| `--consolidate-ms MS` | `HOUND_CONSOLIDATE_MS` | off (0) | With publish-swap: batch publishes every MS; `0` = publish each write |

```bash
./build/hound --fuzzy-backend bk --ranker tie_break \
  --publish-swap --consolidate-ms 200 \
  --load examples/sample.csv --snapshot /tmp/hound.snap --port 8080
```

### Use case & trade-off (per flag)

| Flag / choice | Use when | Trade-off |
|---------------|----------|-----------|
| `--fuzzy-backend symspell` (default) | Read-heavy autocomplete after bulk/`prepare` | Faster fuzzy @ ~20k; higher RSS; ingest builds a delete map |
| `--fuzzy-backend bk` | Tight RAM, frequent rebuilds, parity/oracle tests | Much slower fuzzy at scale; smaller footprint |
| `--ranker linear` | Product score should pull results when text is close | Must understand α; blended order ≠ strict text-first |
| `--ranker tie_break` | Want text primacy, then score, then id | No α knob; different `score` semantics in the JSON |
| `--publish-swap` | Mixed R/W; protect search p99 from write locks | Upserts deep-copy; writes slower / more RAM spike |
| `--consolidate-ms N` | Publish-swap + high write rate; amortize publish cost | Search (and snapshot saves) can lag ~N ms behind live upserts until consolidate/`prepare()` |
| `--load` | Boot from export (pattern A) | Upserts into empty or snapshot-loaded index; orphans need restart or deletes — [`sync-reload.md`](sync-reload.md) |
| `--snapshot` | Avoid full CSV import on every restart | Not a DB; recovery path is still export. Can lag under consolidate — [`snapshot.md`](snapshot.md) |
| `--host` / `--port` | Container / systemd bind | `127.0.0.1` is safer on shared hosts; no auth in MVP — [`threat-model.md`](threat-model.md) |

### Suggested combos

| Workload | Flags |
|----------|--------|
| Bulk once, mostly search | defaults (SymSpell + `shared_mutex` + `linear`) |
| Interactive upserts + hot search | `--publish-swap --consolidate-ms 200` |
| Tiny VM / rare search | `--fuzzy-backend bk` |
| “Name match first, then popularity” | `--ranker tie_break` (or `?ranker=tie_break`) |

---

## See also

- Sync: [`sync-writethrough.md`](sync-writethrough.md), [`sync-reload.md`](sync-reload.md)
- Hydrate: [`hydrate.md`](hydrate.md)
- Perf / SymSpell numbers: [`REFINEMENT.md`](REFINEMENT.md), README Advanced
