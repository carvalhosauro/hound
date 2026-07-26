# Graduate — when to leave Hound

Hound is a **front door**: typo-tolerant candidate ids + your `external_score`,
beside the RDBMS. Graduate when product needs outgrow three fields + hydrate.

Roadmap ids **D3.1–D3.4**. Related: README [Graduate](../README.md#graduate).

---

## Checklist (D3.1)

Leave (or add a second search system) when **several** of these are true:

| Signal | Why Hound is the wrong hammer |
|--------|-------------------------------|
| You need **facets** / aggregations in search results | Not in scope |
| **Filters must run inside** the search engine at scale | Today: filter-after in app/DB; in-index attrs are Phase H (future) |
| **Multi-region** active-active search clusters | One process, full-RAM sidecar |
| Team wants a **full document search product** (UI, synonyms admin, …) | Meili / Typesense / ES |
| Index must hold **fat documents** as source of truth | Violates the sidecar premise |
| Ops needs **shard rebalancing, ILM, cross-cluster** | Elasticsearch / OpenSearch territory |

Stay when: suggest/autocomplete, thin projection, hydrate from Postgres/MySQL,
one box or small VM, and you already own the business score in SQL.

---

## Field mapping (D3.2)

Same mental model — different product names.

### → Meilisearch

| Hound | Meilisearch |
|-------|-------------|
| `id` | primary key (`uid` / document id) |
| `text` | searchable attribute(s) |
| `external_score` | custom ranking rule / sortable attribute |
| App hydrate by id | Keep — or embed fields if you abandon sidecar purity |

### → Elasticsearch / OpenSearch

| Hound | ES / OS |
|-------|---------|
| `id` | `_id` |
| `text` | `text` / `search_as_you_type` field |
| `external_score` | numeric field + `function_score` / `rank_feature` |
| App hydrate by id | Keep, or `_source` if documents become fat |

### → Typesense

| Hound | Typesense |
|-------|-----------|
| `id` | `id` |
| `text` | query-by field |
| `external_score` | field in `sort_by` |

---

## App migration notes (D3.3)

1. Keep `WHERE id IN (…)` hydrate — only swap the client that calls search.
2. Re-point sync: same push of `{id, text, score-like field}` into the new engine
   (write-through or bulk), matching [`sync-writethrough.md`](sync-writethrough.md).
3. Revisit ranking: Meili/ES ranking DSLs replace `α` / `tie_break`; re-validate
   with product, don’t assume identical order.

Minimal diff shape:

```text
search(q) → engine.client.search(q) → [ids…] → db.hydrate(ids) → UI
```

---

## What does **not** port (D3.4)

| Hound-only / Hound-shaped | Notes |
|---------------------------|--------|
| `--fuzzy-backend` SymSpell vs BK | Engine-specific typo settings instead |
| `--publish-swap` / `--consolidate-ms` | Concurrency model is Hound’s |
| Binary `--snapshot` | Use the other product’s snapshot/dump |
| “Three fields only” discipline | Easy to abandon — don’t dump the whole row unless you mean to |

There is **no** one-click migrate. The win is a small API surface and the hydrate
habit, not a dump/restore wizard.
