# 14: Upstream Gamedata intake

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Hosting and publishing).

**What to build:** When the fork syncs upstream, a script compares upstream's bundled `gamedata.json` with the data repo's newest snapshot. For any differing key, it opens a data-repo PR that lists each key with the upstream value, our value and the upstream commit. Keys that only exist in our shape (Slot Binding fields, kill list) are never removed.

**Blocked by:** 08

**Status:** resolved

**Tier:** 4 add-on · later

- [x] Replay on a past upstream change (e.g. the Nov 13 `ClientPrint` / `CCSNavArea_IsValidNavMesh` edits) produces a PR with exactly those keys
- [x] No differences → no PR
- [x] Our additive fields survive the intake

## Comments

- 2026-11-27: Resolved. Data repo `tools/upstream_intake.py` + shared `tools/pull_request.py` (`propose`): per-value diff of upstream `gamedata.json` against the newest snapshot per OS, never removes our additive fields, keeps blanked kills. Replay of the Nov 13 edits proposes exactly ClientPrint and CCSNavArea_IsValidNavMesh; today: no differences. Skipped: automatic trigger on fork sync.
