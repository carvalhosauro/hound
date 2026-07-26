# Sync recipe A — full reload

Periodic export from the RDBMS → CSV/JSON → reload Hound.
**Best when:** small corpora, rare writes, simplest ops.
**Lag:** minutes to hours (however often the job runs).

Design context: [`REFINEMENT.md`](REFINEMENT.md) § Sync · roadmap id **D2.1**.

Hound never queries your DB. This job **pushes** a thin projection
`{ id, text, external_score }`.

---

## Contract

| Step | Owner | Notes |
|------|-------|-------|
| `SELECT` projection | You | Synthetic column names below — map to your schema |
| Write `docs.csv` / `docs.json` | Job | Same shape as [`examples/sample.csv`](../examples/sample.csv) |
| Reload Hound | Job / orchestrator | Prefer **restart + `--load`** so deletes in the DB disappear |

Upserts alone do **not** remove ids that vanished from the export. A true full
reload needs a fresh process (or explicit `DELETE` for orphans).

---

## 1. Export (sketch)

Postgres-shaped (rename table/columns to yours; keep the three Hound fields):

```sql
COPY (
  SELECT
    id::text AS id,
    name AS text,
    coalesce(quality_score, 0)::float8 AS external_score
  FROM items
  WHERE deleted_at IS NULL
) TO STDOUT WITH CSV HEADER;
```

MySQL-shaped:

```sql
SELECT id, name AS text, COALESCE(quality_score, 0) AS external_score
FROM items
WHERE deleted_at IS NULL
INTO OUTFILE '/tmp/docs.csv'
FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '"'
LINES TERMINATED BY '\n';
```

Or any language that writes:

```csv
id,text,external_score
1,Ada Ash,10.5
2,Blake Brook,3.0
```

---

## 2. Reload

### Docker (replace file + recreate)

```bash
# Assume ./data/docs.csv is the fresh export (synthetic sample works too):
cp examples/sample.csv data/docs.csv

docker run --rm -p 8080:8080 \
  -v "$PWD/data/docs.csv:/data/docs.csv:ro" \
  ghcr.io/carvalhosauro/hound:main \
  --host 0.0.0.0 --port 8080 --load /data/docs.csv
```

### Binary

```bash
# Stop the old process, then:
./build/hound --host 0.0.0.0 --port 8080 --load /path/to/docs.csv
```

### Optional snapshot (faster restart next time)

```bash
./build/hound --host 0.0.0.0 --port 8080 \
  --load /path/to/docs.csv \
  --snapshot /var/lib/hound/hound.snap
```

See [`snapshot.md`](snapshot.md) for durability expectations.

---

## 3. Cron-style loop

```cron
# Nightly rebuild at 03:15 (example)
15 3 * * * /opt/hound/bin/reload_from_db.sh >> /var/log/hound-reload.log 2>&1
```

`reload_from_db.sh` sketch:

```bash
#!/usr/bin/env bash
set -euo pipefail
OUT=/var/lib/hound/docs.csv
# psql / mysql / app export → "$OUT"
systemctl restart hound   # unit runs: hound --load "$OUT" --snapshot ...
```

---

## 4. Verify

```bash
curl -sS "http://127.0.0.1:8080/health"
curl -sS "http://127.0.0.1:8080/search?q=ada&limit=5"
```

Demo against a running server (synthetic bulk, not a DB): 
[`scripts/examples/sync_reload_demo.sh`](../scripts/examples/sync_reload_demo.sh).

---

## When to prefer pattern B

Interactive UIs that need suggest within seconds of a write →
[`sync-writethrough.md`](sync-writethrough.md). Many teams run **B** day-to-day
and keep **A** as a nightly safety net against drift.
