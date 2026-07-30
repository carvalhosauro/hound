# Error catalog

Predictable HTTP and process exit codes for operators (DX **D1.4** / **D7.4**).
Wire shapes: [`openapi.yaml`](openapi.yaml). Auth / network:
[`threat-model.md`](threat-model.md).

Error JSON (HTTP 4xx) always looks like:

```json
{ "error": "<message>" }
```

The `error` **string** may change across patch releases; status codes and the
presence of the `error` key are stable ([`compat.md`](compat.md)).

---

## HTTP status codes

| Status | When | Body |
|--------|------|------|
| **200** | Success (`/health`, upsert, search, delete found) | Route-specific success JSON |
| **400** | Bad request: missing/`q`, invalid JSON, non-array bulk, invalid `ranker`, bad numeric params, invalid `attrs` body / empty `attrs.` search key | `{ "error": "…" }` |
| **404** | `DELETE /index/{id}` when id is absent | `{ "error": "not found" }` |

No auth → no **401**/**403** today. Unhandled server faults are not specially
shaped (httplib defaults); treat unexpected 5xx as “process unhealthy.”

---

## Common `error` messages (informative, not a closed enum)

| Endpoint | Typical `error` | Cause |
|----------|-----------------|--------|
| `GET /search` | `missing q` | No `q` query param |
| `GET /search` | `invalid ranker (use linear or tie_break)` | Unknown `ranker` |
| `GET /search` | `stoul` / `stod` / `stoi` what() | Non-numeric `limit` / `alpha` / `max_edit_distance` |
| `GET /search` | `attrs key must be non-empty` | Query key `attrs.` with empty attr name (e.g. `attrs.=x`) |
| `POST /index` | JSON / `at("id")` / type errors | Missing fields or wrong types |
| `POST /index` | `attrs must be an object` | `attrs` present but not a JSON object |
| `POST /index` | `attrs key must be non-empty` | Empty string key inside `attrs` |
| `POST /index` | `attrs values must be strings` | Non-string value in `attrs` |
| `POST /index/bulk` | same `attrs` messages as single upsert | Bad `attrs` on one array element |
| `POST /index/bulk` | `body must be a JSON array` | Object or scalar body |
| `POST /index/bulk` | per-item parse errors | Bad element in the array |
| `DELETE /index/{id}` | `not found` | Id not in index (**404**) |

Empty `results: []` on **200** is success (no matches), not an error.

---

## Process exit codes (`hound` / `hound_bulk_load`)

| Code | Meaning |
|------|---------|
| **0** | Clean exit (`--help`, or server stopped after successful listen return — rare) |
| **1** | Failed to bind `--host`/`--port` |
| **2** | CLI usage error (unknown arg, missing value, invalid flag value, `--consolidate-ms` without publish-swap) |

Snapshot load failures at boot log a **warning** and continue with an empty /
partial index — they do **not** by themselves exit non-zero.

`tools/bulk_load`: **2** = bad args; **0** = success (see that tool’s stderr).

---

## `/health` (ops)

`GET /health` returns **200** with:

| Field | Meaning |
|-------|---------|
| `status` | Always `"ok"` if the process answered |
| `version` | Build version (`CMake` `PROJECT_VERSION`) |
| `size` | Documents currently visible to search |
| `publish_mode` | `"legacy"` or `"publish_swap"` |
| `consolidate_ms` | Background publish interval (0 = publish each write / N/A in legacy) |

Use `size` and `publish_mode` for readiness probes that care about load and
concurrency mode — keep the handler cheap (no fuzzy work).
