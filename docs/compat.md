# HTTP compatibility policy

How Hound treats the public HTTP surface across releases. Machine-readable
contract: [`openapi.yaml`](openapi.yaml). Human `/search` knobs:
[`search-params.md`](search-params.md). Roadmap: **D7.1** / **D1.2** / **D5.0**.

---

## What is stable

These are the **compatibility surface**. Breaking them requires a **major**
version bump (`vN` → `vN+1`) and a clear note in the release / changelog.

| Stable | Notes |
|--------|--------|
| Routes | `GET /health`, `POST /index`, `POST /index/bulk`, `DELETE /index/{id}`, `GET /search` |
| Document JSON | `id` (string), `text` (string), `external_score` (number, default `0`) |
| Search response envelope | `{ "results": [ { "id", "score", "text_relevance", "external_score" } ] }` |
| Search query params | `q` (required), `limit`, `alpha`, `max_edit_distance`, `ranker` |
| Success / error shapes | Success uses `ok` / `status` / `results` as today; errors use `{ "error": string }` with HTTP 4xx |
| Idempotent upserts | Same `id` replaces; bulk is an array of documents |

**Semver intent (once tagged releases are routine — see D7.2):**

- **MAJOR** — remove/rename a stable field, route, or status meaning; change
  required request shape; change default search JSON fields.
- **MINOR** — additive: new optional query/body fields, new routes, richer
  `/health`, new optional response keys (clients must ignore unknowns).
- **PATCH** — fixes, docs, perf, internal fuzzy/backend defaults that do not
  change the wire contract.

Until the first numbered `vX.Y.Z` release with a changelog entry, treat
`main` / GHCR `:main` as **pre-1.0**: still aim not to break the table above
without a loud note, but the formal gate is “document the break.” How to cut a
tag: [`release.md`](release.md). `:latest` is published **only** from `v*` tags.

---

## What may change without a major

| May change | Examples |
|------------|----------|
| Process flags / env | `--fuzzy-backend`, `--publish-swap`, `--consolidate-ms`, bind defaults |
| Ranking / fuzzy internals | SymSpell vs BK, adaptive edit-distance tables, score numerics for the same corpus |
| Error *message* text | `error` string wording (status code + presence of `error` stay) |
| Performance / RSS | Absolute latency and memory |
| Snapshot binary format | May require re-`--load` / rebuild across versions — see [`snapshot.md`](snapshot.md) |
| Docs layout | Guides move; OpenAPI path stays under `docs/openapi.yaml` unless announced |

Optional query aliases (e.g. `ranker=score_merger` → linear) may be added; do
not rely on undocumented aliases in long-lived clients — prefer
`linear` / `tie_break`.

---

## How breaking changes ship

1. Prefer **additive** MINOR changes (new optional fields / routes).
2. If a break is necessary: bump **MAJOR**, update [`openapi.yaml`](openapi.yaml)
   in the same change, note migration in release notes / changelog.
3. Avoid silent renames of JSON keys on `/search` or document upserts.
4. Auth, TLS, and multi-tenant tenancy are **out of MVP**; when they appear,
   they should be opt-in and documented under the threat-model track (**D7.3**),
   not as a silent default that breaks trusted-network deploys.

---

## Auth & network (stance)

No authentication on the HTTP API today. Bind to a trusted network (or put a
reverse proxy / mesh in front). Full notes: [`threat-model.md`](threat-model.md)
(**D7.3** / **D4.4**).

---

## Client guidance

1. Treat [`openapi.yaml`](openapi.yaml) as the wire source of truth.
2. Ignore unknown JSON keys in responses (forward-compatible MINOR additions).
3. Do not require exact floating-point equality on `score` / `text_relevance`
   across versions or backends — compare ranking intent and `id` sets in tests.
4. Prefer curl / a thin hand-written client until OpenAPI is stable enough for
   codegen (**D5**).

---

## Source of truth in code

Handlers: `include/hound/http_api.hpp` (`setup_routes`). If code and OpenAPI
diverge, **fix the OpenAPI in the same PR** as the handler change.
