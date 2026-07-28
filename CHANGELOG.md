# Changelog

All notable changes to Hound are documented here.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versioning follows [SemVer](https://semver.org/) as described in
[`docs/compat.md`](docs/compat.md) and [`docs/release.md`](docs/release.md).

## [Unreleased]

### Added

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

### Changed

- Default fuzzy backend remains SymSpell; BK via `--fuzzy-backend bk`.
- `GET /health` JSON gains additive fields (clients must ignore unknowns).

When cutting `vX.Y.Z`, move the notes above into a new
`## [X.Y.Z] — YYYY-MM-DD` section (see [`docs/release.md`](docs/release.md)).
Until the first tag, prefer GHCR `:main` or a commit SHA for pinning.
