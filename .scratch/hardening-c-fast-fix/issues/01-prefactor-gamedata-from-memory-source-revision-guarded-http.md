# 01: Prefactor: Gamedata from memory, SourceRevision build id, guarded HTTP client

Spec: `.scratch/hardening-c-fast-fix/spec.md`. ADR-0006.

**What to build:** Three small changes that make the feed easy to add.

1. The core's Gamedata config can be built from in-memory JSON bytes, not only from a file path. Boot behaviour stays the same: the bundled file is still read from disk.
2. The build identity CounterStrikeSharp reads from `steam.inf` includes `SourceRevision` next to `PatchVersion`, and the startup report shows it.
3. Every cpp-httplib client construction and call in the legacy updater is guarded. An `https://` or malformed `AutoUpdateURL` logs an error and boot continues; today it aborts the server (upstream #931).

**Blocked by:** None (can start immediately; spec A and B are on `hardening/impl`)

**Status:** ready-for-agent

**Tier:** 0 prefactor · must

- [ ] Gamedata config loads from bytes; existing path-based boot is unchanged; doctest covers both
- [ ] Build identity exposes `SourceRevision` and `PatchVersion`; missing `steam.inf` or fields → "unknown", never a crash; startup report shows both
- [ ] `AutoUpdateURL: "https://..."` and a malformed URL each log one error and the server boots (laptop end to end, Windows path covered by a doctest of the guarded call)
- [ ] No behaviour change otherwise; existing tests pass
