# Snapshot — restart without a full CSV reload

`--snapshot PATH` persists the **published** in-RAM index across process
restarts. Roadmap id **D2.3**.

---

## What it is

| | |
|--|--|
| **On boot** | If the file exists, Hound loads it before optional `--load` |
| **On writes** | Saves after mutations (and after boot `--load` when both are set) |
| **Format** | Hound-private binary — not a substitute for your DB export |

The RDBMS remains source of truth. Snapshot is an **ops convenience** so a
crash/restart does not require waiting on a large CSV import every time.

---

## Usage

```bash
./build/hound --host 0.0.0.0 --port 8080 \
  --load /var/lib/hound/docs.csv \
  --snapshot /var/lib/hound/hound.snap
```

Docker (writable data dir):

```bash
docker run --rm -p 8080:8080 \
  -v "$PWD/data:/data" \
  ghcr.io/carvalhosauro/hound:main \
  --host 0.0.0.0 --port 8080 \
  --load /data/docs.csv \
  --snapshot /data/hound.snap
```

First start: load CSV → build index → write snapshot.  
Later starts: load snapshot quickly; pass `--load` again only when you want to
merge a fresh export (upserts into the loaded index).

---

## Durability expectations (honest)

- **Not** a WAL / multi-node replica.
- Process kill during save can leave a partial file — keep exports as the
  recovery path ([`sync-reload.md`](sync-reload.md)).
- With `--publish-swap --consolidate-ms N`, on-write snapshot saves follow the
  **published** view and can lag live upserts by about N ms until consolidate /
  `prepare()` (see README Advanced).

---

## When to skip it

Tiny corpora where `--load` is already sub-second, or you always rebuild from
export on deploy. Snapshot helps when import time or dependency on the export
job at boot becomes painful.
