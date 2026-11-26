# 07: Kill Switches carry forward

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Gamedata Source). ADR-0006.

**What to build:** When a new build has no snapshot yet and Gamedata is carried forward from build X, X's Kill Switches stay in force on the new build. The startup report says "disabled by gamedata (carried forward from X)". Publishing a snapshot for the new build replaces them.

**Blocked by:** 05, 06

**Status:** ready-for-agent

**Tier:** 2 add-on · must

- [ ] Doctest: a kill in X applies on build Y when Y has no entry; it no longer applies once Y has its own snapshot without the kill
- [ ] The report names the source build of each carried-forward kill
- [ ] End to end: local feed with a kill for build X, server reporting build Y → the feature is disabled and reported as carried forward
