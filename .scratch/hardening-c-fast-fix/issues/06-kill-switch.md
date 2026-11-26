# 06: Kill Switch

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Kill Switch representation, Resolver and report changes). ADR-0002, ADR-0006.

**What to build:** Gamedata can deliberately disable a Binding or an Engine Feature, and that works on any Gamedata, bundled included, so it does not need the feed.

- A blank Binding (empty signature string, null offset or slot) fails with the new reason `DisabledByGamedata`, distinct from `MissingFromGamedata`.
- A top-level `disabledEngineFeatures: [{id, reason}]` disables those Engine Features by stable id with "disabled by gamedata: <reason>". Unknown ids log a warning.
- Killed features count as disabled for `FailMode: crash` and for Required Engine Features.
- New core config `ForceEnableEngineFeatures: [id]` re-enables killed features only, never resolution failures. Every boot logs a warning per forced feature, and the report marks them "forced on by operator".
- The startup report gets a Kill Switch block. The managed query API shows the reason.

**Blocked by:** None (can start immediately; needs spec A's Resolution Report and FailMode, both done)

**Status:** ready-for-agent

**Tier:** 1 base · must

- [ ] Resolver doctest:
  - blank signature, offset and slot → `DisabledByGamedata`
  - feature-level kill disables only that feature
  - force-on re-enables a killed feature but not a failed one
  - unknown id warns
- [ ] `FailMode: crash` with a killed core feature stops the server after the report; with that feature forced on it keeps running
- [ ] Managed API.Tests: a killed feature reports "disabled by gamedata" with its reason; a plugin requiring it refuses to load
- [ ] End to end on the laptop: bundled Gamedata with a blanked `Host_Say` and with `cssharp.server/game-system-events` in the kill list each disable only their feature; the server stays up
- [ ] Windows behaves the same (doctest)
