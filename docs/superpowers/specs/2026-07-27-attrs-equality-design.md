# H1 — Attrs equality filters (design)

Phase **H** slice 1 only: generic document `attrs` + AND equality filters on
search. Multi-index (`/indexes/{name}`) is **slice 2** (separate design/plan
after this ships).

Status: revised 2026-07-27 (post-brainstorming review against current
`Document` / `FuzzyIndex` / HTTP / snapshot). Hybrid API locked: flat routes
remain the `default` corpus; named indexes come later as mirrors.

**Depends on:** [`docs/REFINEMENT.md`](../../REFINEMENT.md) § Phase H0;
[`docs/compat.md`](../../compat.md); HTTP surface in
`include/hound/http_api.hpp`.

**Supersedes** the illustrative H1 sketch in `REFINEMENT.md` (that sketch still
shows numeric attr values and `/indexes/{name}/search`). This doc is the wire
contract for slice 1: **string** attr values, **flat** routes only.

## Goal

Allow apps (e.g. multi-tenant `establishment_id`) to restrict search to an
eligible document set **inside** Hound, without encoding tenant into
`external_score` or relying on fragile filter-after under name collisions.

## Decisions locked

| Topic | Choice |
|-------|--------|
| API shape (long term) | **Hybrid:** flat `/search` `/index` = index `default`; later `/indexes/{name}/…` |
| Ship order | **Attrs first** (this doc); multi-index second |
| Filter semantics v1 | Equality + **AND** only |
| Attr values | **Strings** on the wire (app serializes ids as `"7"`) |
| Attr keys | Non-empty strings; empty key → **400** |
| Missing attr key on doc | Doc **does not** match that filter |
| Omit all `attrs.*` on search | Behavior identical to v0.1.0 (no filter) |
| Omit `attrs` on upsert body | Treat as **empty map** (wipe attrs for that id) |
| Upsert attrs present | Replace map **wholesale** (not deep-merge) |
| Duplicate `attrs.k` query params | **Last wins** (same as other search params) |
| Search JSON response | **Unchanged** — no attrs in hits (hydrate in app) |
| Filter vs rank order | Eligible ∩ candidates **before** rank → then `limit` |
| Semver | **MINOR** additive (v0.2.0 when tagged) |
| Domain types in core | **None** — keys are opaque strings |

## Motivation

Shared SaaS corpus + per-tenant isolation (e.g. `establishment_id`).
Meili/Typesense pattern: one index + filterable tenant field. Filter-after in
the RDBMS fails when many tenants share the same labels (“Arroz” / “Rice”).
In-index equality keeps ranking **inside** the eligible set (H0 intent).

## Scope

### In

- `Document::attrs` as `std::map<std::string, std::string>` (empty OK)
- Attr inverted postings on `IndexState`, maintained in `apply_upsert` /
  `apply_erase` / `clear` (both Legacy and PublishSwap paths; copy ctor must
  copy postings)
- `SearchOptions::attr_filters` — equality filter map
- Search: when filters non-empty, AND-intersect eligible ids with gather
  candidates, **then** rank + limit
- HTTP: optional `attrs` object on `POST /index` and `POST /index/bulk` items
- HTTP: `GET /search?attrs.<key>=<value>&…` (fixed prefix `attrs.`)
- Snapshot **v2** stores attrs; load of v1 fails with an explicit rebuild message
- `--load` JSON bulk: optional `attrs` object (string values); CSV: no attrs
  columns in v1 (empty attrs; document in bulk/DX notes)
- OpenAPI, compat, search-params, errors, REFINEMENT H status, CHANGELOG
- Unit + integration tests; micro gate if search hot path changes

### Out (this slice)

- Multi-index / `/indexes/{name}`
- OR, `IN`, ranges, arrays, nested attrs, facets
- Tenant tokens / auth (app remains the gate; trusted-network unchanged)
- Returning attrs in `/search` hits
- Eligible-first full scan / gather-inside-eligible optimization (follow-up if
  miss-rate shows up)
- CSV attr columns
- Changing default fuzzy/ranker/concurrency

## Document + query shape

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

Invalid upsert / bulk item:

- `attrs` present but not a JSON object
- any attr value not a JSON string
- empty attr key (`""`)

→ **400** `{ "error": "…" }`.

Search: `attrs.` with empty key (`attrs.=x`) → **400**. Non-empty keys with any
string value (including empty string) are valid filters.

## Search algorithm (v1)

Current gather already collects prefix + fuzzy candidates, then ranks, then
truncates to `limit`. With filters:

```text
if attr_filters empty:
  existing path (gather → rank → limit)   # hot path unchanged in structure
else:
  eligible = AND intersection of attr_postings[key][value] for each filter
  if any filter has no posting bucket → eligible = ∅
  gather candidates as today (into by_id / candidates)
  candidates = { c ∈ candidates | c.id ∈ eligible }
  rank(candidates) → limit
```

Implementations may skip non-eligible ids inside `consider` instead of a
post-pass; semantics are identical.

**Honest limitation:** if text gather returns mostly ids outside `eligible`
(e.g. crowded labels across tenants + tight `prefix_candidate_limit`), results
can be short of `limit`. Mitigations for callers: higher `limit`; selective
attrs; future eligible-first gather. Typical eligible sets ~10²–10³ are fine.

## Postings maintenance

`IndexState` gains:

```text
attr_postings: key → value → set<id>
```

| Mutation | Postings |
|----------|----------|
| upsert (new id) | insert id into each `(key,value)` bucket from `doc.attrs` |
| upsert (replace) | remove id from **previous** doc’s attr buckets, then insert new |
| erase | remove id from that doc’s attr buckets, then drop doc |
| clear | clear `attr_postings` |

Publish-swap: `IndexState` copy/assign must copy `attr_postings` (same as
`docs` / `trie` / `fuzzy`).

## Snapshot

- `kSnapshotVersion` **1 → 2**
- Per document after `external_score`: `u32` attr count, then `(key, value)`
  strings (same `write_string` / `read_string` helpers as id/text)
- Load of version ≠ 2: reject. Prefer message that mentions rebuild, e.g.
  `snapshot: unsupported version (rebuild with --load / bulk; v1 not migrated)`
- Document in [`docs/snapshot.md`](../../snapshot.md)

## Compat

| Stable (unchanged) | Additive |
|--------------------|----------|
| Routes, search hit fields, required doc fields | Optional `attrs` on write |
| Omit filters → same ranking path | Optional `attrs.*` query params |
| Error envelope `{ "error": string }` | New 400 cases for bad attrs |

Update [`docs/compat.md`](../../compat.md) Document JSON + search query rows in
the same change set as OpenAPI.

## Acceptance (H1 attrs slice)

Maps to REFINEMENT **H2** attrs bullets only (multi-index stays unchecked until
slice 2):

- [ ] Attr equality filters are generic string keys/values; no domain types
- [ ] Default `{id,text,external_score}` with empty/omitted attrs still works
- [ ] Omit `attrs.*` → identical search behavior to pre-change
- [ ] Synthetic tests only; docs show hydrate-in-app
- [ ] Correctness green; micro **gate** vs baseline (+10%) on unfiltered path
  (`BM_SearchFuzzy/20000/1`, `/2`, `BM_SearchExact/20000`, `BM_Insert/20000`)
- [ ] **Measured** trade-offs published: insert-with-attrs vs insert; filtered
      fuzzy vs unfiltered; under-fetch fixture (shared text × many tenants)
- [ ] Implementation is **test-first** (failing Catch2 before production code)
- [ ] OpenAPI + compat updated in the same change set

## Follow-up (Slice 2 — not this PR series)

Hybrid multi-index: `IndexRegistry`, `/indexes/{name}/…` mirroring flat ops on
`default`, create-on-first-upsert, snapshot layout for multiple corpora. Prefer
**one** index + tenant attr for multi-tenant isolation; named indexes for
divergent verticals (`insumos` vs `produtos`), not one index per tenant.

## Non-goals

- Becoming Meili/ES (facets, DSL body, tenant tokens)
- Hound enforcing auth
- Encoding tenant into `external_score`
- Migrating v1 snapshot files in-process (rebuild from export)
