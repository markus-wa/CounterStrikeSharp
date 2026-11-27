# 10: Bundled Gamedata generated from the data repo

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Hosting and publishing). ADR-0006.

**What to build:** The fork's release build takes the data repo's newest snapshot per OS, merges it into the bundled `gamedata.json` shape, and stamps it with its `sourceRevision`. The fork repo's `gamedata.json` becomes a generated file: CI fails if it differs from the data repo. Edits to Gamedata go to the data repo only. The release also embeds the current feed `seq` as the floor.

**Blocked by:** 05, 08

**Status:** resolved

**Tier:** 3 · must

- [x] The release artifact's `gamedata.json` equals the data repo's newest snapshot (per OS) plus the stamp
- [x] A fork CI check fails when the committed `gamedata.json` drifts from the data repo
- [x] The embedded `seq` floor matches the data repo's last published `seq` at build time
- [x] Contributor docs say where Gamedata is edited now

## Comments

- 2026-11-27: Resolved on `spec-c/10-bundled-from-data-repo`. `eng/gamedata/bundle.py` (gamedata / seq-floor / check), per-OS bundled files in the release, `gamedata` CI job with drift check (also covers the fixture copy), `CONTRIBUTING.md` Gamedata section. Review fix: seq floor read only from a signature-verified index (keys parsed from `kBuiltinKeys`). Seq floor 0 until 09 sets `GAMEDATA_FEED_URL`. Workflow never run on GitHub.
