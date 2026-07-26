# Sync recipe B — write-through

On create/update, the app (or a small worker) upserts into Hound; on delete, it
calls `DELETE`. **Best when:** interactive suggest, near-real-time.
**Lag:** seconds (or ~N ms with `--publish-swap --consolidate-ms N`).

Design context: [`REFINEMENT.md`](REFINEMENT.md) § Sync · roadmap id **D2.2**.

Recommended default for “front door” apps, optionally plus a nightly
[`sync-reload.md`](sync-reload.md) rebuild.

---

## Semantics

| Operation | Hound call | Idempotent? |
|-----------|------------|-------------|
| Insert / update | `POST /index` or `POST /index/bulk` | Yes — same `id` replaces |
| Delete | `DELETE /index/{id}` | Yes — missing id is fine to treat as success in the app |
| Search | `GET /search?q=…` | Read-only |

Hound is **not** the source of truth. If the HTTP call fails, retry or reconcile
with a later full reload. Do not invent a second schema in Hound.

---

## App sketch (any language)

After a successful DB commit:

```bash
# Upsert one doc
curl -sS -X POST "http://127.0.0.1:8080/index" \
  -H 'content-type: application/json' \
  -d '{"id":"42","text":"Ada Ash","external_score":10.5}'

# Bulk upsert
curl -sS -X POST "http://127.0.0.1:8080/index/bulk" \
  -H 'content-type: application/json' \
  -d '[{"id":"42","text":"Ada Ash","external_score":10.5},{"id":"7","text":"Blake Brook","external_score":3}]'

# Delete
curl -sS -X DELETE "http://127.0.0.1:8080/index/42"
```

Pseudo-flow:

```text
DB transaction commit
  → POST /index { id, text, external_score }   # same id = upsert
  → (optional) ignore Hound errors + alert; nightly A repairs drift

DB soft/hard delete commit
  → DELETE /index/{id}
```

Map `text` to whatever users type against (name, title, …) and
`external_score` to your business quality signal.

---

## Flags that matter under write churn

| Load shape | Suggested flags |
|------------|-----------------|
| Mostly reads after boot | defaults |
| Mixed search + frequent upserts, care about search p99 | `--publish-swap --consolidate-ms 200` |
| Tight RAM | `--fuzzy-backend bk` |

Details: README [Advanced](../README.md#advanced).

---

## Hydrate after search

Hound returns ranked ids; the app loads full rows from the DB:

```sql
SELECT * FROM items WHERE id IN (...);  -- order in app by Hound score order
```

Cookbook: [`hydrate.md`](hydrate.md).

---

## Verify

```bash
curl -sS -X POST "http://127.0.0.1:8080/index" \
  -H 'content-type: application/json' \
  -d '{"id":"99","text":"Zed Zinc","external_score":1}'
curl -sS "http://127.0.0.1:8080/search?q=zed&limit=3"
curl -sS -X DELETE "http://127.0.0.1:8080/index/99"
```

Synthetic demo script:
[`scripts/examples/sync_writethrough_demo.sh`](../scripts/examples/sync_writethrough_demo.sh).
