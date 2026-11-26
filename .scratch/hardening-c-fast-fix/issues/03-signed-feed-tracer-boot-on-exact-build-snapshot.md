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

**Status:** ready-for-agent

**Tier:** 1 base · must

- [ ] The Gamedata Source takes an injected transport; the only real transport is a thin cpp-httplib wrapper with every call guarded
- [ ] Doctest (fake transport):
  - RFC 8032 Ed25519 vectors pass
  - a flipped byte in the index, signature or snapshot is rejected before parse
  - an unknown key id is rejected
  - a missing prefix is rejected
  - no entry for this build → bundled
  - feed unreachable → bundled with a warning
- [ ] Python-signed fixture indexes verify natively; mutated ones fail (cross-implementation check)
- [ ] Operator-set `GamedataFeedPublicKeys` replaces the built-in keys (doctest)
- [ ] Legacy updater removed; `GamedataFeedEnabled: false` boots on bundled with no network access
- [ ] End to end on the laptop with a local signed feed (python http.server): exact-build snapshot used and reported; tampered index → bundled with the reason logged
- [ ] Windows builds and uses the same path (doctest; no live Windows run required)
