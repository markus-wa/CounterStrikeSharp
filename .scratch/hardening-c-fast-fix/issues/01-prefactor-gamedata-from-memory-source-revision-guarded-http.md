# 01: Prefactor: Gamedata from memory, SourceRevision build id, guarded HTTP client

Spec: `.scratch/hardening-c-fast-fix/spec.md`. ADR-0006.

**What to build:** Three small changes that make the feed easy to add.

1. The core's Gamedata config can be built from in-memory JSON bytes, not only from a file path. Boot behaviour stays the same: the bundled file is still read from disk.
2. The build identity CounterStrikeSharp reads from `steam.inf` includes `SourceRevision` next to `PatchVersion`, and the startup report shows it.
3. Every cpp-httplib client construction and call in the legacy updater is guarded. An `https://` or malformed `AutoUpdateURL` logs an error and boot continues; today it aborts the server (upstream #931).

**Blocked by:** None (can start immediately; spec A and B are on `hardening/impl`)

**Status:** resolved

**Tier:** 0 prefactor · must

- [x] Gamedata config loads from bytes; existing path-based boot is unchanged; doctest covers both
- [x] Build identity exposes `SourceRevision` and `PatchVersion`; missing `steam.inf` or fields → "unknown", never a crash; startup report shows both
- [x] `AutoUpdateURL: "https://..."` and a malformed URL each log one error and the server boots (laptop end to end, Windows path covered by a doctest of the guarded call)
- [x] No behaviour change otherwise; existing tests pass

## Comments

- 2026-11-27: Resolved on `spec-c/01-prefactor`, merged into `hardening/spec-c`. `bindings::ParseGamedataBytes` + `CGameConfig::InitFromBytes`; `BuildIdentity{patchVersion, sourceRevision}` (report build id is now `1.42.0.1 (SourceRevision 11268945)`, ServerVersion dropped); guarded `http::MakeClient` (https without OpenSSL left a null client, the real #931 cause). Doctests + e2e. Windows not compiled.
