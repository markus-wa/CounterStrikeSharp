# 07: Kill Switches carry forward

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Gamedata Source). ADR-0006.

**What to build:** When a new build has no snapshot yet and Gamedata is carried forward from build X, X's Kill Switches stay in force on the new build. The startup report says "disabled by gamedata (carried forward from X)". Publishing a snapshot for the new build replaces them.

**Blocked by:** 05, 06

**Status:** resolved

**Tier:** 2 add-on · must

- [x] Doctest: a kill in X applies on build Y when Y has no entry; it no longer applies once Y has its own snapshot without the kill
- [x] The report names the source build of each carried-forward kill
- [x] End to end: local feed with a kill for build X, server reporting build Y → the feature is disabled and reported as carried forward

## Comments

- 2026-11-27: Resolved on `spec-c/07-kills-carry-forward`; reshaped in review fixes: the carried-forward source build now comes from provenance (no per-kill field). Kill Switch block lines read `<id>: <reason>, carried forward from <build>`; feature lines use the spec's `disabled by gamedata: <reason>`. Doctests + e2e `carried-forward` case.
