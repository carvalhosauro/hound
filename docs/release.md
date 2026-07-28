# Cutting a release

Release hygiene for Hound (DX **D7.2**). Wire stability rules stay in
[`compat.md`](compat.md). Container publish: `.github/workflows/publish-ghcr.yml`.
Binary + GitHub Release: `.github/workflows/release.yml`.

---

## Versioning

- Tags: **`vX.Y.Z`** (leading `v`, three numeric components).
- Align `project(hound VERSION …)` in `CMakeLists.txt` and `info.version` in
  [`openapi.yaml`](openapi.yaml) with the tag (without the `v` prefix).
- Semver meaning for HTTP: [`compat.md`](compat.md).

| Kind | When |
|------|------|
| **MAJOR** | Breaking HTTP / document / search JSON contract |
| **MINOR** | Additive API or notable operator features |
| **PATCH** | Fixes, docs, perf that do not change the wire contract |

Pre-`v1.0.0`: still follow the table; treat breaks as loud majors even if the
product is young.

---

## Artifacts per tag

| Artifact | Source | Notes |
|----------|--------|--------|
| GHCR image `ghcr.io/<owner>/hound:X.Y.Z` | `publish-ghcr.yml` on `v*` | Also `:vX.Y.Z`, `:X.Y`, **`:latest`** |
| GHCR `:main` | push to `main` | **Not** `:latest` — pin CI / tip-of-tree only |
| GitHub Release | `release.yml` on `v*` | Notes + linux **amd64** binary + `SHA256SUMS` |

Arm64 binaries wait for a native arm runner (same policy as multi-arch images).

---

## Maintainer checklist

1. **Changelog** — move `[Unreleased]` notes into a new `## [X.Y.Z] — YYYY-MM-DD`
   section in [`CHANGELOG.md`](../CHANGELOG.md); leave an empty Unreleased stub.
2. **Versions** — bump `CMakeLists.txt` and `docs/openapi.yaml` `info.version`
   if needed.
3. **Commit** on `main` (conventional commit, e.g. `chore(release): prepare vX.Y.Z`).
4. **Tag and push:**
   ```bash
   git tag -a "vX.Y.Z" -m "vX.Y.Z"
   git push origin main "vX.Y.Z"
   ```
5. **Verify CI:**
   - GHCR tags appear (`:X.Y.Z`, `:latest`).
   - GitHub Release has `hound-linux-amd64` + `SHA256SUMS`.
6. Optional: smoke
   ```bash
   docker run --rm -p 8080:8080 ghcr.io/carvalhosauro/hound:X.Y.Z
   curl -sS "http://127.0.0.1:8080/health"
   ```

Do **not** retag `:latest` from `main`. Do **not** force-push release tags.

---

## Operator pins

| Want | Use |
|------|-----|
| Stable “current release” | `:latest` or `:X.Y.Z` |
| Tip of development | `:main` or `sha-<short>` |
| Reproducible deploy | Exact `:X.Y.Z` (prefer over `:latest`) |

Binary installs: download the Release asset for that tag; verify with
`SHA256SUMS`.
