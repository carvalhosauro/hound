# Hound

**Search that sits beside your database — not on top of it.**

Tired of `LIKE '%burger%'` and a random “score” column? Hound is a tiny C++
sidecar: typo-tolerant autocomplete in, ranked `id`s out. Your Postgres/MySQL
stays the source of truth. Your app hydrates the rest.

> Elasticsearch is the Kubernetes of search.  
> Hound is the Portainer — the door you open when you need something better
> than SQL, without standing up a search platform.

MIT. One process. Generic `{ id, text, external_score }` — no business schemas,
no private data in examples (synthetic only).

| Today | With Hound |
|-------|------------|
| `LIKE '%text%'` (slow, no typos) | Prefix + typo-tolerant search |
| “Score” = random × knob in the DB | **Your** real score → `external_score` |
| Dumping full rows into a second store | Index thin docs → **hydrate from the DB** |

```text
BD (truth) ──sync──► Hound { id, text, external_score }
App ──GET /search──► ranked ids ──hydrate──► BD ──► UI
```

---

## Quick start

```bash
docker run --rm -p 8080:8080 ghcr.io/carvalhosauro/hound:latest
# or: docker compose up --build

curl -s 'http://127.0.0.1:8080/search?q=ada%20ash&limit=5'
```

That loads a synthetic sample CSV and serves on `:8080`. Image:
`ghcr.io/carvalhosauro/hound` (`:latest` / `:vX.Y.Z` on tags, `:main` on
`main`). Build from source and contributor commands live under
[Advanced](#advanced).

---

## Mental model

Three fields. That’s the product.

```json
{ "id": "1", "text": "Ada Ash", "external_score": 10.5 }
```

| Field | Who owns it | Why it exists |
|-------|-------------|----------------|
| `text` | You (export from the DB) | What the user types against |
| `external_score` | **You** (job / SQL / app) | Quality, volume, rating — business truth |
| `id` | Your primary key | So the app can hydrate |

Default ranking:

```text
final = α · text_relevance + (1 − α) · normalize(external_score)
```

**Not in the index (on purpose):** distance, personalization, “open now”
rules. Over-fetch ids, then filter/rerank in the app or DB. Optional **`attrs`**
on upsert plus `attrs.<key>` on search restrict ranking to an eligible set
(string values on the wire); response hits still omit attrs — hydrate in the app.

---

## HTTP API

```bash
curl -s localhost:8080/health

curl -s -X POST localhost:8080/index \
  -H 'content-type: application/json' \
  -d '{"id":"1","text":"Ada Ash","external_score":10,"attrs":{"tenant":"17"}}'

curl -s 'localhost:8080/search?q=ada%20ash&limit=5&alpha=0.7'
curl -s 'localhost:8080/search?q=ada&limit=10&attrs.tenant=17'

curl -s -X DELETE localhost:8080/index/1

curl -s -X POST localhost:8080/index/bulk \
  -H 'content-type: application/json' \
  -d '[{"id":"1","text":"Ada Ash","external_score":10},{"id":"2","text":"Blake Brook","external_score":3}]'
```

CSV for `--load` / bulk tools:

```csv
id,text,external_score
1,Ada Ash,10.5
2,Blake Brook,3.0
```

No auth in the MVP — bind to a trusted network.
Threat model: [`docs/threat-model.md`](docs/threat-model.md).

**Contract:** OpenAPI [`docs/openapi.yaml`](docs/openapi.yaml) · compatibility
[`docs/compat.md`](docs/compat.md).

**`GET /search` knobs** (defaults, use cases, trade-offs):
[`docs/search-params.md`](docs/search-params.md). Process flags
(`--fuzzy-backend`, `--publish-swap`, …) are on the same page.

---

## Sync (short)

Hound does **not** query your database. Your app or job **pushes** thin docs
(`POST /index`, bulk, or `--load`). Upserts are idempotent by `id`; deletes are
explicit (or rebuild from a full export).

| Pattern | Guide |
|---------|--------|
| **A** Full reload (export → `--load`) | [`docs/sync-reload.md`](docs/sync-reload.md) |
| **B** Write-through (`POST /index`) | [`docs/sync-writethrough.md`](docs/sync-writethrough.md) |
| Snapshot across restarts | [`docs/snapshot.md`](docs/snapshot.md) |
| Hydrate ids → SQL | [`docs/hydrate.md`](docs/hydrate.md) |

Roadmap index: [`docs/DX.md`](docs/DX.md) (install → sync → graduate → **maturity / D7**).

---

## Graduate

Hound is a **front door**. Leave when the product outgrows three fields +
hydrate. Checklist, field mapping (Meili / ES / Typesense), and honesty box:
[`docs/graduate.md`](docs/graduate.md).

| Stay on Hound when… | Graduate when you need… |
|---------------------|-------------------------|
| Typo-tolerant suggest + your own score | Facets, heavy in-index filters |
| One process beside Postgres/MySQL | Multi-region search clusters |
| App hydrates full rows from the DB | Analytics / full document search UI |

Typical next stops: **Meilisearch** / **Typesense**, **Elasticsearch** /
**OpenSearch**, or **Sonic** (token suggest only).

---

## Advanced

### Build from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/hound --load examples/sample.csv --port 8080
```

Needs CMake ≥ 3.20, C++20, Git (FetchContent). Correctness:
`./scripts/run_correctness.sh`.

| Binary | Role |
|--------|------|
| `build/hound` | HTTP sidecar |
| `build/hound_bulk_load` | CLI bulk ingest → optional snapshot |
| `build/hound_tests` | unit + golden + integration |
| `build-bench/hound_bench_micro` | Google Benchmark micro suite |

### Fuzzy, rankers, concurrency

Full tables (params + flags, use case / trade-off):
[`docs/search-params.md`](docs/search-params.md).

Short defaults:

| Concern | Default | Common override |
|---------|---------|-----------------|
| Fuzzy | SymSpell | `--fuzzy-backend bk` (RAM / write churn) |
| Ranker | `linear` (α = 0.7) | `?ranker=tie_break` or `--ranker tie_break` |
| Concurrency | `shared_mutex` | `--publish-swap --consolidate-ms 200` |
| Restart | cold `--load` | `--snapshot PATH` |

```bash
./build/hound --publish-swap --consolidate-ms 200 \
  --load examples/sample.csv --snapshot /tmp/hound.snap --port 8080
```

### Image & GHCR

Workflow: [`.github/workflows/publish-ghcr.yml`](.github/workflows/publish-ghcr.yml).
If `docker pull` is denied on this public repo, set the GHCR package visibility
to **Public** once (Packages → hound → Package settings).

### Benchmarks

```bash
./scripts/run_micro.sh
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_<ts>.json
./scripts/run_macro.sh   # needs: go install github.com/rakyll/hey@latest
```

Micro ≠ macro (don’t compare µs to hey). When to run what: [AGENTS.md](AGENTS.md).
Profiling: [benchmarks/profiling/README.md](benchmarks/profiling/README.md).

---

## Docs

| Doc | What |
|-----|------|
| [docs/DX.md](docs/DX.md) | Install / docs / sync / graduate / maturity (D7) DX roadmap |
| [docs/openapi.yaml](docs/openapi.yaml) | Machine-readable HTTP API (OpenAPI 3) |
| [docs/compat.md](docs/compat.md) | HTTP compatibility / semver intent |
| [docs/threat-model.md](docs/threat-model.md) | Trusted-network threat model / auth stance |
| [docs/errors.md](docs/errors.md) | HTTP errors + CLI exit codes + `/health` fields |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Build, tests, PR checklist |
| [docs/release.md](docs/release.md) | How to cut a semver release (GHCR + binary) |
| [CHANGELOG.md](CHANGELOG.md) | Notable changes per version |
| [docs/sync-reload.md](docs/sync-reload.md) | Sync pattern A (full reload) |
| [docs/sync-writethrough.md](docs/sync-writethrough.md) | Sync pattern B (write-through) |
| [docs/hydrate.md](docs/hydrate.md) | Hydrate ranked ids from SQL |
| [docs/graduate.md](docs/graduate.md) | When / how to leave Hound |
| [docs/search-params.md](docs/search-params.md) | `/search` params + process flags (trade-offs) |
| [docs/PLANO.md](docs/PLANO.md) | Design & phases |
| [docs/REFINEMENT.md](docs/REFINEMENT.md) | Post-MVP perf + Phase H attrs |
| [AGENTS.md](AGENTS.md) | Contributor workflow |

Core under `include/hound/` stays free of HTTP/CSV. API and ingest sit outside.

---

## License

MIT — [LICENSE](LICENSE).

Star it if you’re escaping `LIKE`. PRs welcome if they keep the sidecar thin —
see [CONTRIBUTING.md](CONTRIBUTING.md).
