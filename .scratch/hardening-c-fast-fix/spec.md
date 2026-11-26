# Spec C: Signed, build-keyed Gamedata Feed and CI per build (I6, I7)

Status: ready-for-agent

Part of the incremental hardening effort (`.scratch/incremental-hardening/`). Governed by ADR-0001 (incremental, upstream-shaped, fork-first), ADR-0002 (fail soft per Engine Feature, Resolution Report, operator fail mode), ADR-0004 (resolution behind a swappable seam) and ADR-0006 (Gamedata Feed trust model). Vocabulary: `GLOSSARY.md`. Builds on spec A (Resolution Report, Engine Features, `FailMode`) and spec B (generator, Slot Bindings). Inputs collected before the grilling: `inputs.md` (this directory).

## Problem Statement

After specs A and B, a Game update that breaks a Binding disables only its Engine Features, and slot shifts heal. But every fix that still needs a human (a new signature, an offset that could not heal, a feature that must be switched off because its behaviour changed) reaches servers only through a full CounterStrikeSharp release that operators must download and install. Most of the remaining degraded time is waiting for that.

The upstream auto-updater that could shortcut this is not usable as it stands:

- It downloads `gamedata.json` over plain HTTP with no signature. Anyone on the path, or anyone who controls the CDN or its publishing action (pinned to a floating `@latest` tag), can feed arbitrary Gamedata to every server. HTTPS was dropped in July 2025 as a packaging fix: linking OpenSSL dynamically made CounterStrikeSharp fail to load on hosts without `libssl.so.1.1` (upstream #929/#930). It was never a security decision.
- It is Windows-only since August 2025 ("temporarily disable on linux", no reason recorded).
- It is not keyed to the game build. It overwrites `gamedata.json` in place with whatever the CDN serves. Today the CDN copy is older than upstream's bundled file in two keys, so Windows servers are downgraded on start.
- The client is fragile: a 300 s connect timeout, unlimited body size, up to 20 redirects, and an `https://` URL in config aborts the server (#931).
- There is no way to switch a single feature off remotely. When Valve changes behaviour, or breaks CounterStrikeSharp's own IGameSystem implementation, servers crash until a release ships.
- Nothing boots a new build automatically. Maintainers learn about breaks from users, and spec B's generator only runs when a human runs it.

## Solution

CounterStrikeSharp fetches Gamedata from a signed **Gamedata Feed**, keyed by the CS2 build it runs on, on both operating systems:

- At boot, the core reads the build's `SourceRevision` from `steam.inf` and fetches a small signed index from the feed (10 s budget). It verifies the signature over the raw bytes before parsing anything, then downloads the full **Gamedata Snapshot** for exactly this build and OS. The snapshot's size and sha256 must match the signed index.
- Verified snapshots are cached per build and never overwritten in place. If the feed has no snapshot for this build yet (the hours after Valve ships), the newest available snapshot is carried forward, from the cache or the bundled file. If the feed is unreachable or anything fails verification, the server boots on the cache or the bundled file. The updater never blocks boot.
- Gamedata applies at boot only. While the server runs, a poll notices newer Gamedata for this build, logs that a restart will apply it, and raises a managed event so an admin plugin can schedule the restart.
- **Kill Switch:** the feed can disable a single Binding (blank value) or a whole Engine Feature (by its stable id, with a reason). The disabled feature goes through spec A's fail-soft path, so a known crash becomes "feature off" without a release. Kill Switches carry forward to new builds until someone publishes for them. Operators can force a feature back on, loudly.
- Every boot's startup report says where Gamedata came from: exact build, carried forward from which build, or bundled; feed `seq`; signing key; Kill Switches.

A CI job, triggered by the build archiver when Valve ships, boots the new build on a headless Linux server, collects the Resolution Report, runs the native test suite and spec B's generator, and opens a PR on the Gamedata data repo with the report diff. A human reviews and merges. On merge, a GitHub Action signs the index and publishes it to the feed.

## User Stories

### Server operator

1. As a server operator, I want Gamedata fixes to reach my server without installing a new CounterStrikeSharp release, so that broken features come back hours sooner.
2. As a server operator on Linux, I want the Gamedata Feed to work on my platform, so that I get the same fast fixes as Windows servers.
3. As a server operator, I want only Gamedata signed by the CounterStrikeSharp maintainers to be accepted, so that nobody on the network path or at the CDN can change what my server binds to.
4. As a server operator, I want tampered or unsigned downloads rejected and discarded, never stored, so that one bad response cannot poison later boots.
5. As a server operator, I want the server to boot even when the feed is down, slow, blocked or misbehaving, so that the updater is never the reason my server is offline.
6. As a server operator, I want the feed check to take at most 10 seconds, so that restarts stay fast.
7. As a server operator, I want Gamedata chosen for the exact build I run, so that I never get data meant for another build.
8. As a server operator, I want my server to use the newest Gamedata available when my build has no entry yet, so that I still get the best known Bindings in the hours after an update.
9. As a server operator, I want a newer bundled `gamedata.json` (from a CounterStrikeSharp upgrade) to win over an older cached snapshot, so that upgrading never boots me on stale data.
10. As a server operator, I want an old signed index replayed by an attacker to be rejected, so that nobody can undo a Kill Switch by serving yesterday's feed.
11. As a server operator, I want the startup report to say which Gamedata was used and why (exact build, carried forward from build X, or bundled; feed seq; signing key), so that I can tell what my server runs on.
12. As a server operator, I want a log line, and a managed event plugins can react to, when newer Gamedata for my build is available, so that I can restart at a good moment (for example between matches).
13. As a server operator, I want CounterStrikeSharp never to restart my server on its own, so that a match is never interrupted.
14. As a server operator, I want a feature the feed disabled to be shown in the startup report as "disabled by gamedata" with the reason, so that I know it is deliberate and not a resolution failure.
15. As a server operator, I want to force a killed feature back on in my config, with a loud warning on every boot, so that I can take the risk on my own server when I need the feature.
16. As a server operator who copied the example config, I want my existing `AutoUpdateEnabled`/`AutoUpdateURL` keys to keep the server booting and to tell me they are deprecated, so that the upgrade needs no config change.
17. As a server operator, I want an `https://` URL or any malformed feed URL in config to log an error and fall back, never abort the server, so that a typo cannot take my server down.
18. As a server operator, I want to turn the feed off in config, so that I can run on bundled Gamedata only (air-gapped or locked-down hosts).
19. As a server operator running my own feed, I want to configure my own feed URL and public keys, so that I can sign my own Gamedata.
20. As a server operator, I want the cache to keep only the last 20 builds, so that the Gamedata directory does not grow without bound.
21. As a server operator on Windows, I want the same feed, verification and fallback behaviour as on Linux, so that both platforms behave the same.

### Tournament operator

22. As a tournament operator running `FailMode: crash`, I want a core Engine Feature disabled by the feed to stop the server like any other disabled core feature, so that a match never runs with a hook switched off.
23. As a tournament operator, I want the startup report to show whether Gamedata was carried forward from an older build, so that I can decide whether to play on an unverified build.

### Plugin author

24. As a plugin author, I want a killed Engine Feature to look the same as any disabled Engine Feature through the existing query API, with the reason "disabled by gamedata", so that my graceful-degradation code needs no change.
25. As a plugin author who declared Required Engine Features, I want my plugin to refuse to load when the feed kills one of them, so that it never runs half-working.
26. As a plugin author, I want to read the Gamedata provenance (source, build, seq) through the query API, so that my plugin can report it.
27. As a plugin author of an admin plugin, I want an `OnGamedataUpdateAvailable` event with the build, seq and the Engine Features whose state would change, so that I can schedule a restart.
28. As a plugin author, I want my plugin-shipped Gamedata to be unaffected by the feed, so that my plugin keeps working as before.

### Gamedata maintainer

29. As a Gamedata maintainer, I want CI to boot every new CS2 build and open a PR with the Resolution Report diff and the generator's regenerated Slot Bindings, so that I start from evidence instead of user reports.
30. As a Gamedata maintainer, I want a PR even when nothing changed, labelled as such, so that every build gets an explicit "verified on this build" entry that I can merge in one click.
31. As a Gamedata maintainer, I want to disable an Engine Feature for a build by adding it to the feed's kill list with a reason, so that a crash caused by changed behaviour or by our own IGameSystem implementation becomes "feature off" within the hour.
32. As a Gamedata maintainer, I want to disable a single Binding by blanking its value, so that I can switch off a broken signature without touching anything else.
33. As a Gamedata maintainer, I want every merge to the data repo to be signed and published automatically, so that publishing is one merge, with no local signing steps.
34. As a Gamedata maintainer, I want upstream's Gamedata fixes to arrive as a PR on the data repo whenever the fork syncs upstream, so that the fork never misses an upstream fix and never drifts.
35. As a Gamedata maintainer, I want the data repo to be the only place Gamedata is edited, so that the bundled file and the feed never disagree.

### CounterStrikeSharp maintainer (fork)

36. As a maintainer, I want the bundled `gamedata.json` generated from the data repo at release time, with a `sourceRevision` stamp, so that the release and the feed cannot drift.
37. As a maintainer, I want the signing key held only in a protected GitHub environment and an offline backup key compiled in, so that losing or leaking one key is recoverable by a release.
38. As a maintainer, I want the publishing workflow's third-party actions pinned by commit SHA, so that a hijacked tag cannot publish Gamedata.
39. As a maintainer, I want no TLS stack and no new dynamically linked library in the native core, so that the packaging failure that removed HTTPS upstream cannot come back.
40. As a maintainer, I want the feed client, verification and selection testable without a network or a game server, so that regressions are caught in CI.
41. As a maintainer, I want CI to boot both the latest fork release and `main` on each new build, so that I can tell a Gamedata break (both fail) from a code regression (only `main` fails).
42. As a maintainer, I want the legacy overwrite updater and its `gamedata.etag` file removed, so that only one update path exists.
43. As a maintainer, I want the feed format, client and publishing to be upstream-shaped and documented, so that they can be offered to upstream if the maintainer agrees.

## Implementation Decisions

### Gamedata Feed format (ADR-0006)

- The feed is static files over plain HTTP. Integrity and authenticity come from signatures, not from the transport.
- **Index:** one JSON file with:
  - `seq`: monotonic unsigned integer, incremented on every publish.
  - `issued_at`.
  - per build `SourceRevision`: `patchVersion` (human label), and per OS `{file, size, sha256}`.
- **Signature:** a detached Ed25519 signature over the index's raw bytes, prefixed with a domain-separation string (`cssharp-gamedata-index-v1` plus a NUL byte). It carries a key id (the first 8 bytes of the public key's sha256).
- **Gamedata Snapshot:** a complete Gamedata file for one build and one OS, in today's shape plus spec B's additive Slot Binding fields plus the kill list below. Snapshots are immutable and never deltas. A snapshot is identified only through the signed index: its size and sha256 are checked before it is parsed.
- **Kill Switch representation:**
  - Binding level: an empty string signature, or a null offset or slot, means "disabled by gamedata". This is distinct from a key missing from Gamedata.
  - Engine Feature level: a top-level list `disabledEngineFeatures: [{id, reason}]`, where `id` is the stable namespaced Engine Feature id from ADR-0002 (e.g. `cssharp.server/game-system-events`).
- The publisher increments `seq`. Clients store the highest `seq` they have accepted and reject any lower one. Each release embeds a `seq` floor (the `seq` at release time), so deleting the state file cannot roll back below the release.

### Trust and keys (ADR-0006)

- Ed25519 through vendored Monocypher, version pinned, using its Ed25519 (SHA-512) variant so standard tooling can produce signatures.
- Trusted public keys are compiled in: the active CI signing key and an offline backup key. Rotating or revoking either key means shipping a release.
- The CI signing key lives as a secret in a protected GitHub environment of the data repo. The offline backup key is kept by the maintainer as an age-encrypted file on offline media.
- Operators can set their own feed URL and public keys. If `GamedataFeedPublicKeys` is set, it replaces the built-in keys. Anyone who can edit the core config already controls the server, so no further scoping is needed.
- Out of the trust model (backlog, ADR-0006 "considered"): an offline root delegating an online key, index expiry, mirrors, and transport TLS.

### Gamedata Source (new native module; the one new seam)

- One module decides which Gamedata the core boots on. Its inputs:
  - the current build (`SourceRevision` and `PatchVersion` from `steam.inf`, and the OS)
  - the trusted keys and the stored `seq` (with the release floor)
  - the cache directory
  - the bundled `gamedata.json` and its `sourceRevision` stamp
  - an injected transport
- Its output is the Gamedata to resolve against, plus a **provenance** value:
  - source: exact build, carried forward from build X (cache or bundled), or bundled
  - build, `seq` and signing key id
  - kill list
  - warnings: feed unreachable, signature invalid, hash mismatch, `seq` rollback, size cap hit, legacy config
- Selection order:
  1. A verified snapshot for exactly this build and OS (freshly fetched or cached).
  2. Otherwise the newest of all verified cached snapshots and the bundled file, by `SourceRevision`, reported as carried forward.
  3. Otherwise the bundled file. Boot never fails because of this module.
- Verify before parse: no index or snapshot byte reaches the JSON parser before its signature or hash is checked. Cached files are re-verified on every load against the stored index. Cache writes are atomic (temp file, fsync, rename).
- Cache: one immutable file per build and OS plus the latest accepted index and its signature. The last 20 builds are kept and older ones pruned at boot. The bundled `gamedata.json` is never modified.
- Kill Switches in a carried-forward snapshot stay in force for the new build.
- Legacy cleanup: `gamedata.etag` is ignored and deleted on first boot. The in-place overwrite updater is removed on both OSes.
- The module feeds the chosen Gamedata into spec A/B's resolver unchanged (ADR-0004 seam). Provenance is attached to the Resolution Report.

### Transport (native)

- An interface for "fetch this URL under these limits". There is one real implementation on the vendored cpp-httplib, and tests use fakes.
- Hard limits:
  - one 10 s wall-clock deadline per boot fetch, covering DNS
  - 3 s to connect
  - index 64 KiB, signature 256 B, snapshot 1 MiB (today's Gamedata is about 9 KB), headers 8 KiB, 2 MiB in total
  - redirects not followed
  - `Accept-Encoding: identity` (no decompression)
- Conditional requests (`ETag` / `If-None-Match`) keep polls cheap.
- Every client construction and call is guarded. An unsupported scheme (`https://`), a malformed URL, or any exception becomes a warning in provenance, never a crash.
- No TLS stack and no new dynamically linked library in the native core.

### Runtime behaviour

- Gamedata applies at boot only. Bindings are not re-resolved at runtime; the Resolution Report stays one immutable value per boot (ADR-0002).
- A background poll runs every 15 minutes by default, using the same transport and verification, off the game thread. When a verified index announces a newer snapshot for the running build, the client stages it in the cache, logs once that a restart will apply it, and raises `OnGamedataUpdateAvailable(build, seq, changedEngineFeatures)` on the game thread. CounterStrikeSharp never restarts the server.

### Resolver and report changes (spec A/B seam, additive)

- New failure reason `DisabledByGamedata` for blank Bindings. Engine Features on the kill list are disabled with reason "disabled by gamedata: <reason>". Both count as disabled for `FailMode: crash` and for Required Engine Features.
- Operator override: a core config list `ForceEnableEngineFeatures: [id]` re-enables killed features (Kill Switch only, never resolution failures). Every boot logs a warning per forced feature, and the report marks them "forced on by operator".
- The startup report gains a Gamedata line (provenance) and a Kill Switch block. The managed query API exposes provenance next to the Engine Feature list.

### Core config (additive)

- New keys:
  - `GamedataFeedEnabled`: default `true` on both OSes in the fork.
  - `GamedataFeedUrl`: the fork's feed.
  - `GamedataFeedPublicKeys`: optional; replaces the built-in keys.
  - `GamedataFeedPollMinutes`: default 15; 0 disables polling.
  - `ForceEnableEngineFeatures`: default empty.
- `AutoUpdateEnabled` and `AutoUpdateURL` are ignored; if present, one deprecation line is logged. Existing configs boot unchanged.
- Upstream-shaped proposal (not part of this fork spec): the feed is opt-in for its first upstream release, following ADR-0001's warn-first pattern.

### Hosting and publishing

- **Data repo:** a separate repository, the only place Gamedata is edited. CI and humans open PRs; a human merges.
- **On merge:** a GitHub Action in a protected environment rebuilds the index, increments `seq`, signs it, and publishes index, signature and snapshots. All third-party actions are pinned by commit SHA.
- **Publisher tooling:** a Python script in the data repo (the `cryptography` package) builds and signs the index. A cross-implementation test keeps it compatible with the native verifier.
- **Host:** GitHub Pages on a custom domain with "Enforce HTTPS" off. A spike first confirms that plain HTTP is served without a redirect and that `ETag`/304 work; if not, Cloudflare R2 behind a custom domain with "Always Use HTTPS" off (the setup upstream already runs). Choosing and wiring the domain is a human step.
- **Bundled file:** the fork's release build generates the bundled `gamedata.json` from the data repo's newest snapshot per OS, stamped with its `sourceRevision`. The fork repo's copy becomes generated.
- **Upstream intake:** when the fork syncs upstream, a script diffs upstream's `gamedata.json` against the data repo and opens a data-repo PR for any differences.

### CI per build (I7)

- Trigger: the build archiver (owned by the PoC repo) archives a new build and then emits a new-build event that CI consumes. There is one Steam poller.
- Host: the maintainer's laptop as a stopgap, Linux only. The maintainer is named as owner in the upstream note. A permanent host is a later ticket.
- Per new build:
  1. Boot a headless Linux dedicated server with the latest fork release, then with `main`, each on the feed's current Gamedata.
  2. Collect each Resolution Report and run the native xUnit suite.
  3. Run spec B's generator against the previous and the new build.
  4. Open a data-repo PR with the regenerated snapshot for the new `SourceRevision`, the report diff (before and after), and test results. Open it even when nothing changed, labelled "no change", so the build gets a verified entry.
  5. A failure on `main` only is reported as a code regression, not a Gamedata issue.
- The PR is the notification channel for now; a Discord webhook comes later.
- No auto-merge and no signature re-derivation. That is auto-publish (T2).

### Windows

- Same feed, verification, selection, Kill Switch and transport on Windows. Slot Bindings stay unverified there (spec B). CI is Linux only, and Windows snapshots are maintained by hand or through upstream intake until Windows binaries are archived.

## Testing Decisions

- Good tests assert behaviour at the seams. They test which Gamedata a boot selects and what provenance it reports for given feed responses, cache and bundled file. They test which Resolution Report results for given Gamedata with Kill Switches. They test what a real server does and reports at boot. Never assert the internals of the HTTP client or the cache layout.
- **Seam 1, Gamedata Source (native, doctest, fake transport):**
  - Signature and hashes:
    - RFC 8032 Ed25519 test vectors pass.
    - A flipped byte in the index, signature or snapshot is rejected before parse, and the download is not stored.
    - An unknown key id is rejected.
    - A missing domain-separation prefix is rejected.
  - Rollback:
    - A lower `seq` is rejected.
    - A `seq` below the release floor is rejected after the state file is deleted.
  - Selection:
    - An exact-build snapshot wins.
    - With no entry for this build, the newest of cache and bundled wins (a newer bundled file beats an older cache).
    - A Kill Switch carries forward.
    - Feed unreachable, a timeout and a garbage response each fall back with a warning in provenance.
  - Cache:
    - Writes are atomic.
    - A tampered cache file is re-verified and rejected.
    - Pruning keeps 20 builds.
    - `gamedata.etag` is deleted.
    - Legacy `AutoUpdate*` keys produce one deprecation warning.
- **Seam 1b, real transport (native, doctest, local test HTTP server):**
  - An oversized body is cut off at its cap.
  - A slow-drip response hits the wall-clock deadline.
  - A redirect is refused.
  - An `https://` URL and a malformed URL warn and do not throw.
  - `If-None-Match` gets a 304.
- **Seam 2, resolver (existing, doctest):**
  - A blank signature, offset or slot gives `DisabledByGamedata`, distinct from `MissingFromGamedata`.
  - A feature-level kill disables the feature with its reason.
  - `ForceEnableEngineFeatures` re-enables only killed features, never failed ones.
  - `FailMode: crash` treats killed core features as disabled.
  - Provenance appears in the report.
- **Managed (API.Tests, existing pattern):**
  - Hand-built Resolution Reports with killed features show "disabled by gamedata" through the query API.
  - Required Engine Features refuse to load.
  - Provenance is exposed.
  - `OnGamedataUpdateAvailable` is dispatched with its payload.
- **Seam 3, end-to-end (laptop server, spec A's harness, local signed test feed):**
  - A boot with an exact-build snapshot reports it.
  - A blanked core Binding and a feature-level kill each disable only their feature.
  - A tampered feed falls back to the cache and logs why.
  - A newer `seq` published while running triggers the restart notice and the event.
  - Feed off boots on bundled.
- **Seam 4, publisher (data repo):**
  - A cross-implementation test: indexes signed by the Python tool verify in the native verifier (fixture files checked into the native tests), and mutated ones fail.
  - A CI dry run on an archived build pair (1.41.9.7 → 1.42.0.1), laptop only, skipped when the archive is absent, produces a PR payload with the expected report diff.
- Prior art: spec A's doctest target and fake-resolver managed tests, spec B's real-build doctests (skipped without the archive), the end-to-end harness and its broken-Gamedata runs.

## Out of Scope

- Runtime re-resolution of Bindings after a feed update (boot only; a runtime swap would be a later ticket if ever needed).
- Auto-publish and signature re-derivation (T2): CI never merges, and humans write signatures.
- Offline root delegation, index expiry, mirrors, transport TLS (including out-of-process HTTPS via WinHTTP or curl), and a .NET background refresher. Recorded as considered in ADR-0006; backlog.
- Plugin-shipped Gamedata through the feed.
- Windows CI and archiving Windows binaries.
- A permanent CI host, and a Discord notification.
- Automatic server restarts.
- Upstream PRs, and offering the feed to upstream (fork PRs only for now; ADR-0001). The upstream note lists the Linux updater and its signing key as open questions for the maintainer.
- tier0/loader link breaks, hard-coded struct layouts (spec E candidate), and the own loader (spec D).

## Further Notes

- **Depends on:**
  - spec A: Resolution Report, Engine Features with stable ids, `FailMode`, Required Engine Features, the end-to-end harness
  - spec B: the generator CLI, and Slot Binding fields that degrade to unverified when absent, which is already implemented
  - the PoC repo's build archiver, for the CI trigger
- Upstream history behind the Problem Statement: #925 introduced the updater (Jul 2025); #929/#930 dropped OpenSSL after load failures on hosts without `libssl.so.1.1`; #931 is the `https://` abort; 1f30e561 (#958) made it Windows-only; the CS2-Gamedata repo syncs `data/` to Cloudflare R2 through `r2-upload-action@latest`.
- The backtest assumes the Kill Switch: it turns IGameSystem (8 incidents) and semantic-change crashes into "feature off" in the A–C numbers.
- Grilling decisions: `.scratch/incremental-hardening/README.md`, spec C grilling section.
- Sliced vertically into 15 tickets under `.scratch/hardening-c-fast-fix/issues/`, each with a `Tier:` line:
  - Tier 0: 01 prefactor (Gamedata from memory, `SourceRevision`, guarded HTTP client); 02 hosting spike (human).
  - Tier 1 base: 03 signed feed tracer; 06 Kill Switch (independent of the feed).
  - Tier 2: 04 hardened transport; 05 cache, carry-forward and rollback; 07 Kill Switches carry forward; 08 data repo and publisher.
  - Tier 3: 09 production keys, environment and domain (human); 10 bundled Gamedata generated from the data repo; 11 CI per build tracer.
  - Tier 4: 12 CI generator output and `main` regression check; 13 restart notice and event; 14 upstream intake; 15 upstream note update.
  - **Must set:** 01–12. **Later:** 13, 14, 15.
