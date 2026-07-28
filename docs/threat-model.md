# Threat model (trusted network)

Auth stance and operator expectations for Hound’s HTTP surface (DX **D7.3** /
**D4.4**). Wire contract: [`openapi.yaml`](openapi.yaml). Compat:
[`compat.md`](compat.md).

This is a **doc-first** MVP: no tokens, no TLS termination inside Hound, no
multi-tenant isolation. Put controls **in front of** or **around** the process.

---

## Assets

| Asset | Sensitivity | Notes |
|-------|-------------|--------|
| Indexed `text` | Often business-visible strings (names, titles) | Not a second DB of secrets — still don’t expose to the internet |
| `id` + `external_score` | Usually non-secret keys / rankings | Enough to map and skew suggest |
| Snapshot file | Full corpus dump | Protect like a DB export ([`snapshot.md`](snapshot.md)) |
| Write endpoints | Integrity of the index | Anyone who can `POST`/`DELETE` can poison or wipe suggest |

Hound is a **sidecar**: the RDBMS remains source of truth. Compromising Hound
hurts search quality and leaks whatever you chose to index — not your full
relational store — unless you indexed secrets (don’t).

---

## Trust boundary

```text
[ clients / app ] ──HTTP──► [ Hound :8080 ] ──(optional)──► snapshot on disk
         ▲
         │  only this network path is trusted
```

**Assumption:** every peer that can open a TCP connection to the bind address
is allowed to **read and write** the full API (search, upsert, bulk, delete).

If that assumption is false, do not expose the port.

---

## Bind defaults

| Context | Default `--host` | Why |
|---------|------------------|-----|
| Binary / CMake | `127.0.0.1` | Loopback only — safe on shared hosts |
| Docker image / Compose | `0.0.0.0` | Published ports must accept container traffic |

On a shared machine, prefer loopback + reverse proxy on localhost, or a private
overlay network. Publishing `0.0.0.0:8080` on a public cloud NIC without a
firewall is an **open writeable search index**.

---

## What Hound does **not** provide (MVP)

- Authentication or authorization (no API keys, JWT, mTLS in-process)
- TLS / HTTPS termination
- Per-tenant indexes or ACLs
- Request rate limiting / WAF
- Audit log of who mutated the index

These are **explicit non-goals** until a real consumer demands them. If auth
lands later, it must be **opt-in** and documented — silent defaults must not
break trusted-network deploys ([`compat.md`](compat.md)).

---

## What to put in front

Pick one (or combine):

| Control | Use when |
|---------|----------|
| **Private network only** | App + Hound on the same VPC / k8s NetworkPolicy / docker network; no public Service |
| **Reverse proxy** | Need TLS, IP allowlists, basic auth, or path ACLs at the edge (nginx, Caddy, Traefik, cloud LB) |
| **Service mesh mTLS** | Already running mesh; treat Hound as an internal workload |
| **Loopback + local proxy** | Hound on `127.0.0.1`; only the colocated app talks to it |

Recommended default for production-shaped deploys: **private network + no
public ingress**. Add TLS at the proxy if traffic crosses untrusted hops.

---

## Threats (honest)

| Threat | Impact | Mitigation today |
|--------|--------|------------------|
| Unauthenticated upsert/delete | Poisoned rankings, empty index | Network isolation; don’t publish the port |
| Unauthenticated search | Leak indexed text / ids / scores | Same; minimize what you put in `text` |
| Large bulk POST | Memory / CPU spike | Network trust; size limits at proxy if needed |
| Snapshot theft | Full corpus copy | File permissions; encrypted volume; don’t bake into public images |
| Dependency / RCE in process | Full host as the hound user | Keep image updated; run as non-root (container UID 10001) |

Out of scope for this doc: supply-chain of the build, physical access, and
compromising the RDBMS itself.

---

## Operator checklist

1. Bind: loopback or private NIC — not a public `0.0.0.0` without a firewall.
2. Confirm no public LoadBalancer / Ingress to Hound unless a proxy enforces auth.
3. Snapshot path: owner-only permissions; not world-readable.
4. Don’t index secrets, tokens, or PII you wouldn’t put in application logs.
5. Prefer GHCR pinned tags for deploys ([`release.md`](release.md)).

---

## Token auth later?

Only if a consumer cannot use network isolation. Preferred shape if demanded:

- Opt-in shared secret (e.g. `Authorization: Bearer …` or header) checked in
  the HTTP layer — **not** per-document ACLs.
- Default remains “no auth” for local / compose DX.

Until then: **trusted network is the auth model.**
