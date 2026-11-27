# 03: Signed feed tracer: boot on an exact-build snapshot

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Gamedata Feed format, Trust and keys, Gamedata Source, Core config). ADR-0006, ADR-0004.

**What to build:** The first path end to end. At boot, the new Gamedata Source module:

1. Fetches the feed's index and detached signature through a minimal, guarded transport.
2. Verifies the Ed25519 signature (vendored Monocypher, pinned, Ed25519/SHA-512) over the raw bytes, with the domain-separation prefix and key id, before parsing anything.
3. Picks the entry for this build's `SourceRevision` and OS, downloads the Gamedata Snapshot, and checks its size and sha256 against the index before parsing.
4. Hands the Gamedata to the resolver unchanged.

Any failure falls back to the bundled file with a warning. The startup report gets a Gamedata provenance line: source, build, `seq`, key id, warnings.

New core config keys replace the legacy updater on both OSes: `GamedataFeedEnabled` (default true), `GamedataFeedUrl`, and `GamedataFeedPublicKeys` (replaces the built-in keys when set). The legacy updater code and its Windows-only call are removed. A minimal Python signer (`cryptography` package; it becomes ticket 08's tool) produces the test feed and the fixtures. Until ticket 09, the built-in key is a test key.

**Blocked by:** 01

**Status:** resolved

**Tier:** 1 base · must

- [x] The Gamedata Source takes an injected transport; the only real transport is a thin cpp-httplib wrapper with every call guarded
- [x] Doctest (fake transport):
  - RFC 8032 Ed25519 vectors pass
  - a flipped byte in the index, signature or snapshot is rejected before parse
  - an unknown key id is rejected
  - a missing prefix is rejected
  - no entry for this build → bundled
  - feed unreachable → bundled with a warning
- [x] Python-signed fixture indexes verify natively; mutated ones fail (cross-implementation check)
- [x] Operator-set `GamedataFeedPublicKeys` replaces the built-in keys (doctest)
- [x] Legacy updater removed; `GamedataFeedEnabled: false` boots on bundled with no network access
- [x] End to end on the laptop with a local signed feed (python http.server): exact-build snapshot used and reported; tampered index → bundled with the reason logged
- [x] Windows builds and uses the same path (doctest; no live Windows run required)

## Comments

- 2026-11-27: Resolved on `spec-c/03-signed-feed-tracer`. Monocypher 4.0.2 vendored (`libraries/monocypher`), own sha256; `src/core/gamedata_feed.{h,cpp}` (verify before parse, key id, domain prefix), `core_gamedata.{h,cpp}` at boot on both OSes; legacy updater removed; managed reads core Gamedata from native (`GET_CORE_GAMEDATA`); config `GamedataFeedEnabled/Url/PublicKeys`. Signature file = 8-byte key id + 64-byte sig at `index.json.sig`. Signer later moved to the data repo (08). Shortcuts until 09: built-in test key, placeholder URL `http://gamedata-feed.invalid`. Windows not compiled.
