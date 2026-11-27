# 04: Hardened transport

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Transport). ADR-0006.

**What to build:** The real transport enforces hard limits, so a hostile or broken server can neither stall boot nor exhaust memory:

- one 10 s wall-clock deadline per boot fetch, covering DNS; 3 s to connect
- caps: index 64 KiB, signature 256 B, snapshot 1 MiB, headers 8 KiB, 2 MiB in total
- redirects refused
- `Accept-Encoding: identity`
- conditional requests with `If-None-Match` and the stored `ETag`

Every breach becomes a warning in provenance and a fallback, never a crash.

**Blocked by:** 03

**Status:** resolved

**Tier:** 2 add-on · must

- [x] Doctest against a local test HTTP server:
  - oversized body cut off at its cap
  - slow-drip response stopped at the deadline
  - unresolvable host stopped at the deadline
  - redirect refused
  - compressed response refused or ignored
  - 304 handled as "unchanged"
- [x] Boot never exceeds the 10 s budget for the feed step (end to end against a blackholed URL)
- [x] Windows uses the same limits (doctest)

## Comments

- 2026-11-27: Resolved on `spec-c/04-hardened-transport`. `Transport::Get(Request{url, maxBytes, ifNoneMatch})` → `FetchResult{body, error, etag, notModified}`; `HttpTransport(Limits{10 s, 2 MiB})` per fetch; 3 s connect, identity encoding, redirects and Content-Encoding refused; Gets on a detached worker so DNS is under the deadline. Doctests against a local httplib server + e2e blackhole (10.0 s). Shortcuts: a hung DNS worker outlives the deadline; cpp-httplib buffers up to 100×8 KiB header lines before the 8 KiB total check (cannot be capped in httplib).
