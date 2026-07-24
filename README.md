# Hound

**Search that sits beside your database — not on top of it.**

Tired of `LIKE '%burger%'` and a random “score” column? Hound is a tiny C++
sidecar: typo-tolerant autocomplete in, ranked `id`s out. Your Postgres/MySQL
stays the source of truth. Your app hydrates the rest.

> Elasticsearch is the Kubernetes of search.  
> Hound is the Portainer — the door you open when you need something better
> than SQL, without standing up a search platform.

MIT. One binary. Generic `{ id, text, external_score }` — no business schemas,
no private data in examples (synthetic only).

---

## The problem it solves

| Today | With Hound |
|-------|------------|
| `LIKE '%text%'` (slow, no typos) | Prefix + fuzzy (SymSpell by default) |
| “Score” = random × knob in the DB | **Your** real score → `external_score` |
| Dumping full rows into a second store | Index thin docs → **hydrate from the DB** |

You keep thinking in SQL. Hound only answers: *which ids match this query, and
how do they rank when we merge text relevance with the score you already
computed?*

```text
BD (truth) ──sync──► Hound { id, text, external_score }
App ──GET /search──► ranked ids ──hydrate──► BD ──► UI
```

---

## 60-second start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/hound --load examples/sample.csv --port 8080
```

```bash
curl -s 'localhost:8080/search?q=ada%20ash&limit=5&alpha=0.7'
```

Requirements: CMake ≥ 3.20, C++20 compiler, Git (FetchContent pulls Catch2,
cpp-httplib, nlohmann/json).

| Binary | Role |
|--------|------|
| `build/hound` | HTTP sidecar |
| `build/hound_bulk_load` | CLI bulk ingest → optional snapshot |
| `build/hound_tests` | unit + golden + integration |
| `build/hound_bench` | legacy summary bench |
| `build-bench/hound_bench_micro` | Google Benchmark micro suite |

```bash
ctest --test-dir build --output-on-failure
# or
./scripts/run_correctness.sh
```

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

Ranking (default):

```text
final = α · text_relevance + (1 − α) · normalize(external_score)
```

Optional `tie_break` ranker: text → external → id (Typesense-style order,
`α` ignored). Per-query: `?ranker=tie_break`.

**Not in the index (on purpose):** distance, personalization, “open now”
business rules. Over-fetch ids, filter/rerank in the app or DB — same pattern
as Portainer: keep the familiar control plane.

Roadmap for generic filter attrs + multi-index: [docs/REFINEMENT.md](docs/REFINEMENT.md)
(Phase H).

---

## HTTP API

```bash
curl -s localhost:8080/health

curl -s -X POST localhost:8080/index \
  -H 'content-type: application/json' \
  -d '{"id":"1","text":"Ada Ash","external_score":10}'

curl -s 'localhost:8080/search?q=ada%20ash&limit=5&alpha=0.7'
# curl -s 'localhost:8080/search?q=ada&max_edit_distance=1'
# curl -s 'localhost:8080/search?q=ada%20ash&ranker=tie_break'

curl -s -X DELETE localhost:8080/index/1

curl -s -X POST localhost:8080/index/bulk \
  -H 'content-type: application/json' \
  -d '[{"id":"1","text":"Ada Ash","external_score":10},{"id":"2","text":"Blake Brook","external_score":3}]'
```

CSV for `--load` / `hound_bulk_load`:

```csv
id,text,external_score
1,Ada Ash,10.5
2,Blake Brook,3.0
```

```bash
# snapshot across restarts
./build/hound --load examples/sample.csv --snapshot /tmp/hound.snap --port 8080
```

No auth in the MVP — bind to a trusted network.

---

## Fuzzy & rankers (knobs that matter)

**Fuzzy backends**

| Backend | Select | Best when | ≈20k synthetic docs |
|---------|--------|-----------|---------------------|
| **SymSpell** (default) | omit / `--fuzzy-backend symspell` | Bulk once, then many searches | Fuzzy ~µs; RSS ~226 MB |
| **BK-tree** | `--fuzzy-backend bk` or `HOUND_FUZZY_BACKEND=bk` | Tight RAM, write churn, oracle tests | Fuzzy ~ms; RSS ~30 MB |

```bash
./build/hound --fuzzy-backend bk --load examples/sample.csv --port 8080
./build/hound --ranker tie_break --load examples/sample.csv --port 8080
./build/hound --publish-swap --load examples/sample.csv --port 8080
```

**Concurrency:** default is `shared_mutex` (concurrent readers, exclusive writers).
Opt-in `--publish-swap` / `HOUND_PUBLISH_SWAP=1` publishes an atomic snapshot so
readers never wait on writers (E2); upserts deep-copy state and are slower.

**Rankers:** `linear` (default) or `tie_break`. Response shape stays
`id`, `score`, `text_relevance`, `external_score`.

---

## Who this is for

- Indie hackers and small teams drowning in `LIKE` + bad ranking
- SaaS apps that already have a DB and refuse a second source of truth
- Anyone who wants typo-tolerant search **and** to keep business score in-house

## Who should look elsewhere

- You need facets, analytics, multi-region clusters → Elasticsearch / OpenSearch
- You want a full document search product with filters day-one → Meilisearch / Typesense
- You only need suggest tokens → Sonic

Hound’s bet is narrower on purpose: **candidate ids + first-class external score
beside the RDBMS.**

---

## Benchmarks

```bash
./scripts/run_micro.sh
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_<ts>.json
./scripts/run_macro.sh   # needs: go install github.com/rakyll/hey@latest
```

Micro: insert / exact / fuzzy / score-merge at 1k · 5k · 20k (synthetic, seed 42).  
Macro: live HTTP + JSON. Don’t compare µs to hey latencies.

When to run what: [AGENTS.md](AGENTS.md). Profiling:
[benchmarks/profiling/README.md](benchmarks/profiling/README.md).

---

## Docs & layout

| Doc | What |
|-----|------|
| [docs/PLANO.md](docs/PLANO.md) | Design & phases |
| [docs/REFINEMENT.md](docs/REFINEMENT.md) | Post-MVP (perf + Phase H attrs) |
| [AGENTS.md](AGENTS.md) | Contributor workflow |

Core under `include/hound/` stays free of HTTP/CSV. API and ingest sit outside.

---

## License

MIT — [LICENSE](LICENSE).

Star it if you’re escaping `LIKE`. PRs welcome if they keep the sidecar thin.
