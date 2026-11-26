# 02: Spike: plain HTTP on a GitHub Pages custom domain

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Hosting and publishing). ADR-0006.

**What to build:** Decide where the Gamedata Feed is hosted. Choose a domain or subdomain the fork controls. Publish a throwaway Pages site on it with "Enforce HTTPS" off, and confirm with a plain-HTTP client (curl over `http://`, and the core's cpp-httplib without TLS) that:

- files are served over HTTP with no redirect to HTTPS
- `ETag` is returned and `If-None-Match` yields 304
- a small JSON file and a binary signature file come back byte-identical

If any check fails, fall back to Cloudflare R2 behind a custom domain with "Always Use HTTPS" off (upstream's proven setup) and record that decision. Note any Pages limits that matter (bandwidth, publish rate).

**Blocked by:** None (can start immediately)

**Status:** ready-for-human (domain choice, DNS)

**Tier:** 0 standalone · must

- [ ] Domain chosen and recorded in the spec's hosting section
- [ ] The three checks pass on Pages, with curl output recorded in the ticket's comments, or R2 is chosen with the reason recorded
- [ ] Throwaway site removed or repurposed for ticket 09
