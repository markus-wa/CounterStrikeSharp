# 12: CI: generator output and main regression check

Spec: `.scratch/hardening-c-fast-fix/spec.md` (CI per build).

**What to build:** The per-build CI job also:

- runs spec B's generator against the previous and the new build, and puts the regenerated Slot Bindings into the PR's snapshot, with flagged (ambiguous) entries listed in the body
- boots fork `main` next to the latest release

A failure that happens only on `main` is labelled a code regression, not a Gamedata issue.

**Blocked by:** 11; spec B 04 (generator carry-forward)

**Status:** ready-for-agent

**Tier:** 4 add-on · must

- [ ] Dry run on 1.41.9.7 → 1.42.0.1: the PR snapshot carries the ISource2Server slots at +1, and nothing is wrong
- [ ] Flagged generator entries appear in the PR body and are not written into the snapshot
- [ ] Release passes and `main` fails → PR labelled "code regression"; both fail → labelled "gamedata"
