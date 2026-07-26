# Hound — DX roadmap

Living plan for **developer experience** and **operator experience**: make Hound
the easy, low-resource **front door** to search, with a clear path to graduate
to Meilisearch / Elasticsearch when product needs grow.

Related: product/perf roadmap in [`REFINEMENT.md`](REFINEMENT.md), design in
[`PLANO.md`](PLANO.md). Contributor build rules: [`AGENTS.md`](../AGENTS.md).

---

## Product premise (locked intent)

| Pillar | Promise |
|--------|---------|
| **Friendly DX** | 60-second first search without reading the fuzzy/concurrency guts |
| **Few resources** | One process, thin `{ id, text, external_score }`, full-RAM by design |
| **Sidecar** | RDBMS stays source of truth; app hydrates |
| **Front door** | Start here; graduate to Meili/ES when facets, filters-at-scale, or clusters win |

Hound is **not** trying to out-feature Meili/Typesense/ES. It wins on time-to-first-search,
mental model size, and ops surface — then gets out of the way.

---

## Current DX snapshot (honest)

| Area | Today | Gap vs premise |
|------|-------|----------------|
| Mental model | Strong — 3 fields, hydrate pattern | Keep |
| HTTP API | Small (`/health`, `/index`, `/search`, bulk, delete) | Stable; document OpenAPI later |
| Local start | CMake + C++20 | Friction vs `docker run` |
| Docker / releases | Dockerfile + compose + GHCR workflow | First tag push + package Public; multi-arch / binary releases |
| Sync from DB | DIY HTTP/CSV | Need 1–2 copy-paste recipes |
| Auth | None (trusted network) | Document threat model; optional later |
| SDKs | curl only | Optional thin clients |
| Graduate path | README “look elsewhere” | Need explicit when/how checklist |
| Advanced knobs | Visible early in README | Push behind “Advanced” |

---

## Principles for DX work

1. **Happy path first.** Default docs show Docker (or one binary) + CSV/JSON load + one `curl`.
2. **Progressive disclosure.** SymSpell/BK, publish-swap, consolidate-ms live under Advanced.
3. **One page per job.** Sync, snapshot, graduate — separate short guides, not one novel.
4. **Measure DX.** Prefer “time to first ranked id” and “lines of app glue” over micro benches.
5. **No second source of truth.** DX never invents schemas that fight the RDBMS.
6. **English** user-facing docs (project rule).

---

## Roadmap (small, shippable slices)

Order is intentional: install → docs clarity → sync → graduate → polish → optional platforms.

### D0 — Install front door ✅ / next

| ID | Delivery | Done when | Status |
|----|----------|-----------|--------|
| **D0.1** | Multi-stage `Dockerfile` + `.dockerignore` | `docker build` → image; `docker run` serves `/health` + sample search | **Done** |
| **D0.2** | `docker compose` one-file demo (hound + sample load) | `docker compose up` → curl works | **Done** |
| **D0.3** | README “60-second start” leads with Docker; CMake secondary | First screen is container/binary | **Done** (compose-first) |
| **D0.4** | CI release artifacts (linux amd64/arm64 binaries) | GitHub Release assets; checksums | Later |
| **D0.5** | Publish image to GHCR on tag / `main` | `ghcr.io/carvalhosauro/hound:<tag>` pullable | **Done** (workflow; needs first successful run + Public package) |

```bash
# D0 happy path (published)
docker run --rm -p 8080:8080 ghcr.io/carvalhosauro/hound:latest
curl -s 'http://127.0.0.1:8080/search?q=ada%20ash&limit=5'

# Local build
docker compose up --build
```

### D1 — Docs for humans (not only agents)

| ID | Delivery | Done when |
|----|----------|-----------|
| **D1.1** | Split README: Quick start · Mental model · API · Advanced · Graduate | New user never hits SymSpell before first search |
| **D1.2** | OpenAPI (or equivalent) for HTTP surface | Machine-readable; linked from README |
| **D1.3** | “Hydrate pattern” cookbook (SQL `WHERE id IN (…)`) | Copy-paste for Postgres + MySQL |
| **D1.4** | Error catalog (HTTP 4xx/5xx + CLI exit codes) | Predictable ops debugging |
| **D1.5** | Contributor DX: link `AGENTS.md` + “what to run when” table stays current | PRs don’t guess benches |

### D2 — Sync recipes (RDBMS → Hound)

Patterns and non-goals are specified in [`REFINEMENT.md`](REFINEMENT.md) § Sync
(A full reload · B write-through · C outbox/CDC · D bus). This section ships
**copy-paste recipes** only.

| ID | Delivery | Done when |
|----|----------|-----------|
| **D2.1** | Recipe: periodic CSV export + `--load` / bulk (**pattern A**) | Documented cron-style loop |
| **D2.2** | Recipe: app upserts on write (`POST /index`) (**pattern B**) | Documented with idempotent id semantics |
| **D2.3** | Recipe: snapshot for restart (`--snapshot`) | Clear durability expectations (full reload to RAM) |
| **D2.4** | Optional: tiny `scripts/examples/sync_*.sh` (synthetic only) | Runnable demos; no real PII |

Out of scope until demanded: Kafka/Rabbit/CDC **inside** Hound (bus stays outside —
see Phase H / Sync non-goals).

### D3 — Graduate path (front door → Meili / ES)

| ID | Delivery | Done when |
|----|----------|-----------|
| **D3.1** | “When to graduate” checklist (facets, filters-in-index, multi-region, team size) | Decision tree in docs |
| **D3.2** | Field mapping cheat sheet `{id,text,external_score}` → Meili / ES | One table each |
| **D3.3** | App-layer migration notes (keep hydrate; swap search client) | Minimal app diff described |
| **D3.4** | Honesty box: what does **not** port (Hound-only knobs) | No false “one click migrate” |

### D4 — API / product polish (still thin)

| ID | Delivery | Done when |
|----|----------|-----------|
| **D4.1** | Optional `fields=id` projection (**G1**) | Default JSON unchanged |
| **D4.2** | Consistent `/search` query-param docs + examples | All knobs in one table |
| **D4.3** | Health richer? (`size`, version, publish mode) — keep cheap | Ops-friendly without leaking internals |
| **D4.4** | Trusted-network auth story (doc first; token later if needed) | Explicit non-goals for MVP |

### D5 — Client DX (optional)

| ID | Delivery | Done when |
|----|----------|-----------|
| **D5.0** | Publish OpenAPI for HTTP (blocks serious codegen) | Spec checked in; linked from README |
| **D5.1** | Official curl cookbook (bash) | Cover upsert/search/bulk/delete |
| **D5.2** | Thin typed client (pick **one**: JS or Python), **hand-written** | `clients/` or npm/PyPI; mirrors OpenAPI |
| **D5.3** | Optional codegen experiment (openapi-generator / Fern / Speakeasy) | Only if D5.2 maintenance hurts; same public API |
| **D5.4** | Second language only if D5.2 sees real use | Avoid SDK sprawl |

**SDK strategy (locked preference):**

1. **Today:** no SDK — `curl` + any HTTP client is enough (API is tiny).
2. **Next:** write **OpenAPI** by hand (or from comments) — one source of truth.
3. **First SDK:** **manual** thin wrapper (fetch/httpx) — 4–6 methods, types for
   `{id,text,external_score}` and search hits. Faster to review than codegen for
   this surface area.
4. **Codegen tools** (optional later): [OpenAPI Generator](https://openapi-generator.tech/),
   [Fern](https://buildwithfern.com/), [Speakeasy](https://www.speakeasy.com/),
   [Stainless](https://www.stainless.com/) — useful when many languages or the
   OpenAPI grows. **Do not** start with codegen before OpenAPI exists; the tool
   does not invent the API.

You do **not** have to hand-write every language forever — but the first client
should be hand-written so the contract stays honest. Codegen is a scaling
choice, not a bootstrap requirement.

### D6 — Contributor / maintainer DX

| ID | Delivery | Done when |
|----|----------|-----------|
| **D6.1** | `docs/DX.md` (this file) kept in sync with shipped install story | Status column truthful |
| **D6.2** | Devcontainer or compose for contributors (build + test) | Optional; don’t block D0 |
| **D6.3** | Pre-commit / CI checklist mirrored in short CONTRIBUTING | One path for humans + agents |
| **D6.4** | Sample dataset generator documented (synthetic only) | Benches/examples stay reproducible |

---

## Explicit non-goals (for now)

- Replacing Meili/Typesense/ES feature sets
- Multi-node cluster DX
- GUI admin console
- Cloud-hosted SaaS control plane
- Hound speaking Kafka natively

Those belong to “graduate” or a different product.

---

## Suggested ship order (next 4 slices)

1. **D1.1** — README restructure (Quick start / Advanced / Graduate stub)  
2. **D2.1 + D2.2** — two sync recipes  
3. **D3.1 + D3.2** — graduate checklist + field mapping  
4. **D0.4** — optional binary GitHub Release (GHCR workflow is in place; publish on next `main`/tag push)

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
