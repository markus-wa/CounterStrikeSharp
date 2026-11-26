# 10: Bundled Gamedata generated from the data repo

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Hosting and publishing). ADR-0006.

**What to build:** The fork's release build takes the data repo's newest snapshot per OS, merges it into the bundled `gamedata.json` shape, and stamps it with its `sourceRevision`. The fork repo's `gamedata.json` becomes a generated file: CI fails if it differs from the data repo. Edits to Gamedata go to the data repo only. The release also embeds the current feed `seq` as the floor.

**Blocked by:** 05, 08

**Status:** ready-for-agent

**Tier:** 3 · must

- [ ] The release artifact's `gamedata.json` equals the data repo's newest snapshot (per OS) plus the stamp
- [ ] A fork CI check fails when the committed `gamedata.json` drifts from the data repo
- [ ] The embedded `seq` floor matches the data repo's last published `seq` at build time
- [ ] Contributor docs say where Gamedata is edited now
