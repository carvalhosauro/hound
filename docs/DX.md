# Hound — DX roadmap

Living plan for **developer experience** and **operator experience**: make Hound
the easy, low-resource **front door** to search, with a clear path to graduate
to Meilisearch / Elasticsearch when product needs grow.

Related: product/perf roadmap in [`REFINEMENT.md`](REFINEMENT.md), design in
[`PLANO.md`](PLANO.md). Contributor build rules: [`AGENTS.md`](../AGENTS.md).

---

## Guides (shipped)

| Job | Doc |
|-----|-----|
| Install / 60s search | [README Quick start](../README.md#quick-start) |
| Full reload from DB export | [`sync-reload.md`](sync-reload.md) (**A**) |
| Write-through upserts | [`sync-writethrough.md`](sync-writethrough.md) (**B**) |
| Snapshot across restarts | [`snapshot.md`](snapshot.md) |
| Hydrate ids → SQL rows | [`hydrate.md`](hydrate.md) |
| `/search` params + process flags | [`search-params.md`](search-params.md) |
| When / how to leave Hound | [`graduate.md`](graduate.md) |

Synthetic demos (need a running Hound):

```bash
./scripts/examples/sync_reload_demo.sh
./scripts/examples/sync_writethrough_demo.sh
# HOUND_URL=http://127.0.0.1:8080  # default
```

---

## Product premise (locked intent)

| Pillar | Promise |
|--------|---------|
| **Friendly DX** | 60-second first search without reading the fuzzy/concurrency guts |
| **Few resources** | One process, thin `{ id, text, external_score }`, full-RAM by design |
| **Sidecar** | RDBMS stays source of truth; app hydrates |
| **Front door** | Start here; graduate to Meili/ES when facets, filters-at-scale, or clusters win |
| **Maturity** | Predictable releases and a stable HTTP contract — trust before virality |

Hound is **not** trying to out-feature Meili/Typesense/ES. It wins on time-to-first-search,
mental model size, and ops surface — then gets out of the way.

**Maturity vs popularity:** maturity = semver, stable API, honest ops docs, evidence someone
else can run it without guessing. Popularity = distribution and narrative (niche ceiling;
not a DX milestone). Roadmap **D7** tracks maturity only.

---

## Current DX snapshot (honest)

| Area | Today | Gap vs premise |
|------|-------|----------------|
| Mental model | Strong — 3 fields, hydrate pattern | Keep |
| HTTP API | Small (`/health`, `/index`, `/search`, bulk, delete) | OpenAPI later (**D1.2**) |
| Local start | Docker / GHCR first; CMake secondary | Package Public after first GHCR push |
| Docker / releases | Dockerfile + compose + GHCR workflow | Multi-arch; optional binary releases (**D0.4**) |
| Sync from DB | Recipes **A** / **B** + snapshot + demos | CDC/bus templates only if demanded |
| Search / flags docs | [`search-params.md`](search-params.md) | — |
| Hydrate | Postgres + MySQL cookbook | — |
| Auth | None (trusted network) | Threat model doc (**D4.4** / **D7.3**) |
| SDKs | curl only | Optional thin clients after OpenAPI |
| Graduate path | Checklist + field mapping | — |
| Release hygiene | GHCR on `main` / `v*`; no binary assets yet | Semver policy + changelog + **D0.4** (**D7**) |
| API contract | Small HTTP surface; no OpenAPI | OpenAPI + compatibility notes (**D7.1**) |
| Production evidence | Synthetic benches + local demos | External / dogfood write-up (**D7.5**) |
| Advanced knobs | Behind README Advanced | Keep |

---

## Principles for DX work

1. **Happy path first.** Default docs show Docker (or one binary) + CSV/JSON load + one `curl`.
2. **Progressive disclosure.** SymSpell/BK, publish-swap, consolidate-ms live under Advanced.
3. **One page per job.** Sync, snapshot, graduate — separate short guides, not one novel.
4. **Measure DX.** Prefer “time to first ranked id” and “lines of app glue” over micro benches.
5. **No second source of truth.** DX never invents schemas that fight the RDBMS.
6. **English** user-facing docs (project rule).
7. **Predictability over features.** Prefer a boring upgrade and a frozen `/search` shape over
   new knobs. Breaking HTTP changes need an explicit decision (see **D7.1**).

---

## Roadmap (small, shippable slices)

Order is intentional: install → docs clarity → sync → graduate → polish → **maturity** →
optional platforms / clients.

### D0 — Install front door ✅

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D0.1** | Multi-stage `Dockerfile` + `.dockerignore` | `docker build` → image; `docker run` serves `/health` + sample search | **Done** |
| **D0.2** | `docker compose` one-file demo (hound + sample load) | `docker compose up` → curl works | **Done** |
| **D0.3** | README “60-second start” leads with Docker; CMake secondary | First screen is container/binary | **Done** |
| **D0.4** | CI release artifacts (linux amd64/arm64 binaries) | GitHub Release assets; checksums | Later |
| **D0.5** | Publish image to GHCR on tag / `main` | `ghcr.io/carvalhosauro/hound:<tag>` pullable | **Done** (set package Public once) |

```bash
docker run --rm -p 8080:8080 ghcr.io/carvalhosauro/hound:latest
curl -s 'http://127.0.0.1:8080/search?q=ada%20ash&limit=5'
```

### D1 — Docs for humans ✅ / next

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D1.1** | Split README: Quick start · Mental model · API · Advanced · Graduate | New user never hits SymSpell before first search | **Done** |
| **D1.2** | OpenAPI (or equivalent) for HTTP surface | Machine-readable; linked from README | Later |
| **D1.3** | “Hydrate pattern” cookbook (SQL `WHERE id IN (…)`) | Copy-paste for Postgres + MySQL | **Done** — [`hydrate.md`](hydrate.md) |
| **D1.4** | Error catalog (HTTP 4xx/5xx + CLI exit codes) | Predictable ops debugging | Later |
| **D1.5** | Contributor DX: link `AGENTS.md` + “what to run when” table stays current | PRs don’t guess benches | Later |

### D2 — Sync recipes (RDBMS → Hound) ✅

Patterns and non-goals: [`REFINEMENT.md`](REFINEMENT.md) § Sync.
**Copy-paste recipes** (not connectors in-core):

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D2.1** | Recipe: periodic CSV export + `--load` / bulk (**pattern A**) | Documented cron-style loop | **Done** — [`sync-reload.md`](sync-reload.md) |
| **D2.2** | Recipe: app upserts on write (`POST /index`) (**pattern B**) | Documented with idempotent id semantics | **Done** — [`sync-writethrough.md`](sync-writethrough.md) |
| **D2.3** | Recipe: snapshot for restart (`--snapshot`) | Clear durability expectations | **Done** — [`snapshot.md`](snapshot.md) |
| **D2.4** | Optional: tiny `scripts/examples/sync_*.sh` (synthetic only) | Runnable demos; no real PII | **Done** |

Out of scope until demanded: Kafka/Rabbit/CDC **inside** Hound (bus stays outside).

**Default for front-door apps:** pattern **B** day-to-day, optional nightly **A** as drift safety net.

### D3 — Graduate path (front door → Meili / ES) ✅

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D3.1** | “When to graduate” checklist | Decision tree in docs | **Done** — [`graduate.md`](graduate.md) |
| **D3.2** | Field mapping `{id,text,external_score}` → Meili / ES / Typesense | One table each | **Done** |
| **D3.3** | App-layer migration notes (keep hydrate; swap search client) | Minimal app diff described | **Done** |
| **D3.4** | Honesty box: what does **not** port | No false “one click migrate” | **Done** |

### D4 — API / product polish (still thin)

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D4.1** | Optional `fields=id` projection (**G1**) | Default JSON unchanged | Later |
| **D4.2** | Consistent `/search` query-param docs + flag trade-offs | All knobs in one table | **Done** — [`search-params.md`](search-params.md) |
| **D4.3** | Health richer? (`size`, version, publish mode) — keep cheap | Ops-friendly without leaking internals | Later |
| **D4.4** | Trusted-network auth story (doc first; token later if needed) | Explicit non-goals for MVP | Later |

### D5 — Client DX (optional)

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D5.0** | Publish OpenAPI for HTTP (blocks serious codegen) | Spec checked in; linked from README | Later |
| **D5.1** | Official curl cookbook (bash) | Cover upsert/search/bulk/delete | Later |
| **D5.2** | Thin typed client (pick **one**: JS or Python), **hand-written** | `clients/` or npm/PyPI; mirrors OpenAPI | Later |
| **D5.3** | Optional codegen experiment | Only if D5.2 maintenance hurts | Later |
| **D5.4** | Second language only if D5.2 sees real use | Avoid SDK sprawl | Later |

**SDK strategy (locked preference):** curl today → hand-written OpenAPI → one thin
manual client → codegen only if the surface grows. See earlier notes in git history
/ D5 section intent: do not start with codegen.

### D6 — Contributor / maintainer DX

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D6.1** | This file kept in sync with shipped install story | Status column truthful | Ongoing |
| **D6.2** | Devcontainer or compose for contributors (build + test) | Optional; don’t block D0 | Later |
| **D6.3** | Pre-commit / CI checklist mirrored in short CONTRIBUTING | One path for humans + agents | Later |
| **D6.4** | Sample dataset generator documented (synthetic only) | Benches/examples stay reproducible | Later |

### D7 — Product maturity (trust / predictability)

Goal: make Hound **boring to operate** — not famous. Maturity here means a stranger can
pick a tagged release, sync a small corpus, search, restart from snapshot, and know what
will / will not break on the next minor. Popularity (Show HN, stars, default-of-category)
is out of scope for this table.

Cross-links reuse existing IDs where the work already lived; **D7.\*** are the maturity
framing and the gaps that had no home.

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D7.1** | HTTP compatibility policy + OpenAPI checked in | Short `docs/compat.md` (or README section): what is stable, how breaks ship; OpenAPI linked (**D1.2** / **D5.0**) | Later |
| **D7.2** | Release hygiene | Semver tags (`vX.Y.Z`); `CHANGELOG.md` (or GitHub Release notes) for each tag; GHCR `:latest` only from tags; optional binaries (**D0.4**) | Later |
| **D7.3** | Threat model + auth stance | Trusted-network doc: bind defaults, no auth MVP, what to put in front (**D4.4**); token only if demanded | Later |
| **D7.4** | Operator predictability | Error catalog (**D1.4**); richer `/health` (`size`, version, publish mode) (**D4.3**); snapshot durability expectations already in [`snapshot.md`](snapshot.md) | Later |
| **D7.5** | External evidence (dogfood) | One public write-up or issue: real-shaped (still synthetic OK) corpus size, RSS, sync pattern used, “would / would not use again” — not a marketing post | Later |
| **D7.6** | Maintainer path | Short `CONTRIBUTING.md` (**D6.3**): build, `run_correctness`, when to micro/macro | Later |

**Done already (counts toward maturity, not re-listed as open work):** Docker/GHCR front
door (**D0**), progressive README (**D1.1**), hydrate + sync A/B + snapshot (**D1.3**,
**D2**), graduate checklist (**D3**), search-params guide (**D4.2**).

**Explicitly not maturity milestones:** multi-arch until a native arm runner exists;
SDKs beyond one thin client; CDC/Kafka in-core; becoming “default search” culturally.

---

## Explicit non-goals (for now)

- Replacing Meili/Typesense/ES feature sets
- Multi-node cluster DX
- GUI admin console
- Cloud-hosted SaaS control plane
- Hound speaking Kafka natively
- Chasing Portainer/SQLite-scale **popularity** (niche ceiling accepted)

Those belong to “graduate” or a different product (except popularity — that is simply
not a roadmap target).

---

## Suggested ship order (next slices)

1. **D7.1** — compatibility policy + OpenAPI (**D1.2** / **D5.0**)  
2. **D7.2** — semver Release notes + optional **D0.4** binaries  
3. **D7.3** / **D4.4** — trusted-network threat model (doc)  
4. **D7.4** — error catalog + richer `/health` (**D1.4**, **D4.3**)  
5. **D7.6** — short CONTRIBUTING (**D6.3**)  
6. **D7.5** — dogfood write-up when there is something real to say  
7. **D4.1** — optional `fields=id` (**G1**) only if a client asks  

Perf/structure work stays in [`REFINEMENT.md`](REFINEMENT.md) (F/H/G). Do not block DX
on ART or attrs unless a recipe needs them.

---

## How to contribute to DX

- Prefer a **one-PR slice** with a user-visible win (command that works, page that
  shortens time-to-first-search).
- Synthetic examples only; no real schemas/PII.
- Update this file’s Status column when a D-id ships.
- Link new guides from README; don’t orphan docs.

---

## Changelog (DX)

### 2026-07-26 — Product maturity track (D7)

- Added pillar **Maturity** (predictability over virality) and principle #7.
- Added **D7** slices: compat/OpenAPI, release hygiene, threat model, operator
  predictability, dogfood evidence, CONTRIBUTING — maturity only, not popularity.
- Non-goal: chasing Portainer/SQLite-scale popularity.
- Suggested ship order reprioritized around **D7.1 → D7.6**.

### 2026-07-26 — Search params & flag trade-offs (D4.2)

- Added [`search-params.md`](search-params.md): `/search` query params, adaptive
  edit-distance table, process flags, use-case / trade-off per knob, suggested combos.
- README Advanced points here instead of duplicating long flag prose.

### 2026-07-26 — Sync / hydrate / graduate guides (D1.3, D2, D3)

- Added [`sync-reload.md`](sync-reload.md), [`sync-writethrough.md`](sync-writethrough.md),
  [`snapshot.md`](snapshot.md), [`hydrate.md`](hydrate.md), [`graduate.md`](graduate.md).
- Added synthetic `scripts/examples/sync_*.sh` demos.
- DX snapshot + roadmap status columns updated; README Sync/Graduate link here.

### 2026-07-26 — README progressive disclosure (D1.1)

- Restructured README: Quick start → Mental model → HTTP API → Sync → Graduate → Advanced.
- SymSpell / concurrency / benches moved under Advanced; first search path is Docker + curl only.

### 2026-07-26 — GHCR publish workflow (D0.5)

- Added `.github/workflows/publish-ghcr.yml` (push `main` → `:main`; tag `v*` → semver + `:latest`).
- README 60s start leads with `ghcr.io/carvalhosauro/hound`.
- Dockerfile: Ninja + BuildKit cache mount on `/src/build` for faster rebuilds.
- Still manual once: set GHCR package visibility to Public after first push.
- Build note: publish `linux/amd64` only; avoid QEMU multi-arch for C++ (use native arm runner later).

### 2026-07-26 — DX roadmap + Docker bootstrap

- Added `docs/DX.md` (premise, gaps, D0–D6 roadmap).
- Added multi-stage `Dockerfile` + `.dockerignore` (D0.1).
- Added `docker-compose.yml` demo (D0.2); README 60s start leads with compose (D0.3).
- Default container CMD: `--host 0.0.0.0 --load /app/examples/sample.csv --port 8080`.
- Binary GitHub Releases (D0.4) remain optional.
