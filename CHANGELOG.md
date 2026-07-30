# Changelog

All notable changes to Hound are documented here.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versioning follows [SemVer](https://semver.org/) as described in
[`docs/compat.md`](docs/compat.md) and [`docs/release.md`](docs/release.md).

## [Unreleased]

### Added

- Optional `attrs` on `POST /index` and bulk items: opaque string map, replaced
  wholesale on upsert; omit `attrs` wipes attrs for that id.
- `GET /search` equality filters via `attrs.<key>=<value>` (AND across keys;
  string values on the wire). Search hits unchanged — hydrate attrs in the app.

### Notes

- MINOR additive surface (target **v0.2.0** when tagged). Flat routes only;
  multi-index remains future work.
- Measured trade-offs at 20k docs: insert-with-attrs ~flat vs plain insert;
  filtered fuzzy ~2.6× unfiltered at D=2 (~6.7× at D=1). Legacy micro gate names
  within +10% vs baseline. Under-fetch possible when shared text spans tenants and
  `limit` is tight — see [`docs/REFINEMENT.md`](docs/REFINEMENT.md) (2026-07-30).

## [0.1.0] — 2026-07-27

First numbered MVP release: fuzzy autocomplete sidecar with external score,
Docker/GHCR front door, sync/graduate docs, and maturity track (compat, OpenAPI,
release hygiene, threat model, richer `/health`, CONTRIBUTING).

### Added

- [`CONTRIBUTING.md`](CONTRIBUTING.md) — build, when to run what, PR checklist
  (DX **D7.6** / **D6.3** / **D1.5**).
- Operator predictability: richer `/health` (`version`, `size`, `publish_mode`,
  `consolidate_ms`) and [`docs/errors.md`](docs/errors.md) — DX **D7.4** /
  **D1.4** / **D4.3**.
- Trusted-network threat model ([`docs/threat-model.md`](docs/threat-model.md)) —
  DX **D7.3** / **D4.4**.
- Release hygiene: [`docs/release.md`](docs/release.md), root `CHANGELOG.md`,
  GitHub Release workflow with linux amd64 binary + checksums (DX **D7.2** /
  **D0.4**). GHCR `:latest` only from `v*` tags.
- HTTP compatibility policy ([`docs/compat.md`](docs/compat.md)) and OpenAPI 3
  contract ([`docs/openapi.yaml`](docs/openapi.yaml)) — DX **D7.1**.
- Product maturity roadmap track (**D7**) in [`docs/DX.md`](docs/DX.md).
- Sync / hydrate / graduate / search-params / snapshot guides + synthetic demos.
- Progressive README (Quick start → Advanced); Docker Compose demo; GHCR publish
  on `main` (`:main`) and `v*` tags (`:latest` + semver).
- Core: SymSpell default fuzzy backend, adaptive edit distance, pluggable
  rankers (`linear` / `tie_break`), opt-in publish-swap + consolidate-ms.

### Changed

- Default fuzzy backend remains SymSpell; BK via `--fuzzy-backend bk`.
- `GET /health` JSON gains additive fields (clients must ignore unknowns).

[Unreleased]: https://github.com/carvalhosauro/hound/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/carvalhosauro/hound/releases/tag/v0.1.0
