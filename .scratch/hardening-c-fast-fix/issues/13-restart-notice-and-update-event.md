# 13: Restart notice and OnGamedataUpdateAvailable

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Runtime behaviour, Resolver and report changes).

**What to build:** While the server runs, a poll (`GamedataFeedPollMinutes`, default 15; 0 disables it) checks the feed off the game thread, using the hardened transport and full verification. When a newer verified snapshot for the running build appears, it is staged in the cache and the client:

- logs once that a restart will apply it
- raises `OnGamedataUpdateAvailable(build, seq, changedEngineFeatures)` on the game thread

CounterStrikeSharp never restarts the server. The Gamedata provenance is exposed to plugins through the query API.

**Blocked by:** 04, 05

**Status:** resolved

**Tier:** 4 add-on · later

- [x] Doctest: a newer `seq` with a changed snapshot stages it and reports the changed features; the same `seq` or a 304 does nothing; a failed verification stages nothing
- [x] Managed API.Tests: the event is dispatched with its payload; provenance is readable
- [x] The poll never blocks the game thread (end to end: tick timing unaffected while the feed is blackholed)
- [x] End to end: publishing a new `seq` to the local feed while the server runs → one log line and one event; the next boot uses it

## Comments

- 2026-11-27: Resolved on `spec-c/13-restart-notice`. Poll thread (`GamedataFeedPollMinutes`, default 15), stages a newer verified snapshot, one restart notice, `OnGamedataUpdateAvailable(build, seq, changes)` via tickScheduler on the game thread; structured provenance in the report + `EngineFeatures.Gamedata` (story 26; kill list added in review fixes). Test plugin `CounterStrikeSharp.Tests.RestartNotice`, e2e `restart-notice.sh`. Shortcuts: changed features = Gamedata diff, not re-resolution; event waits for game frames while hibernating; ETag in memory only.
