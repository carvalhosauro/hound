# Hydrate pattern — ranked ids → full rows

Hound returns **candidate ids** (plus scores). Your app loads the real rows from
the RDBMS. Roadmap id **D1.3**.

---

## Flow

```text
GET /search?q=…  →  [{ "id": "1", "score": … }, …]
                 →  SELECT … WHERE id IN (…)
                 →  order rows to match Hound score order
                 →  UI
```

Preserve Hound’s order in the application (SQL `IN` does not guarantee order).

---

## Postgres

```sql
-- $1 = array of ids from Hound, in rank order
SELECT i.*
FROM items AS i
JOIN unnest($1::text[]) WITH ORDINALITY AS r(id, ord)
  ON i.id::text = r.id
ORDER BY r.ord;
```

Or with `bigint` ids:

```sql
SELECT i.*
FROM items AS i
JOIN unnest($1::bigint[]) WITH ORDINALITY AS r(id, ord)
  ON i.id = r.id
ORDER BY r.ord;
```

---

## MySQL 8+

```sql
-- Bind ids in rank order; FIELD() restores that order.
SELECT *
FROM items
WHERE id IN (?, ?, ?, …)
ORDER BY FIELD(id, ?, ?, ?, …);
```

(Same placeholders twice: once for `IN`, once for `FIELD`.)

---

## App-side order (any DB)

```text
hits = hound.search(q)           # [{id, score}, …]
rows = db.fetch_by_ids(hits.ids) # map id → row
return [rows[id] for id in hits.ids if id in rows]
```

Drop ids that vanished from the DB (stale index) or trigger a sync repair
([`sync-reload.md`](sync-reload.md)).

---

## Filters / “open now” / geo

Apply **after** hydrate (or in the SQL of the hydrate query). Over-fetch
`limit` from Hound if you expect to discard some rows. In-index attrs/filters
are a future product direction (Phase H), not required for the front door.
