# 13: Restart notice and OnGamedataUpdateAvailable

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Runtime behaviour, Resolver and report changes).

**What to build:** While the server runs, a poll (`GamedataFeedPollMinutes`, default 15; 0 disables it) checks the feed off the game thread, using the hardened transport and full verification. When a newer verified snapshot for the running build appears, it is staged in the cache and the client:

- logs once that a restart will apply it
- raises `OnGamedataUpdateAvailable(build, seq, changedEngineFeatures)` on the game thread

CounterStrikeSharp never restarts the server. The Gamedata provenance is exposed to plugins through the query API.

**Blocked by:** 04, 05

**Status:** ready-for-agent

**Tier:** 4 add-on · later

- [ ] Doctest: a newer `seq` with a changed snapshot stages it and reports the changed features; the same `seq` or a 304 does nothing; a failed verification stages nothing
- [ ] Managed API.Tests: the event is dispatched with its payload; provenance is readable
- [ ] The poll never blocks the game thread (end to end: tick timing unaffected while the feed is blackholed)
- [ ] End to end: publishing a new `seq` to the local feed while the server runs → one log line and one event; the next boot uses it
