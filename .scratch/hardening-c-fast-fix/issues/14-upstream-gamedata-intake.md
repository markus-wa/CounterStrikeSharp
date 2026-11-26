# 14: Upstream Gamedata intake

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Hosting and publishing).

**What to build:** When the fork syncs upstream, a script compares upstream's bundled `gamedata.json` with the data repo's newest snapshot. For any differing key, it opens a data-repo PR that lists each key with the upstream value, our value and the upstream commit. Keys that only exist in our shape (Slot Binding fields, kill list) are never removed.

**Blocked by:** 08

**Status:** ready-for-agent

**Tier:** 4 add-on · later

- [ ] Replay on a past upstream change (e.g. the Nov 13 `ClientPrint` / `CCSNavArea_IsValidNavMesh` edits) produces a PR with exactly those keys
- [ ] No differences → no PR
- [ ] Our additive fields survive the intake
