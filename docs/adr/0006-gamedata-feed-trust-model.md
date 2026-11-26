# Gamedata Feed: signed index over plain HTTP, verified before parse

Spec C delivers Gamedata to servers from a fork-owned Gamedata Feed on both OSes. Its security rests entirely on signatures, not on the transport:

- One index lists per build (`SourceRevision`) and OS the snapshot's size and sha256, plus a monotonic `seq`. It is signed with a detached Ed25519 signature (vendored Monocypher) over its raw bytes, with a domain-separation prefix and key id.
- The client verifies before parsing: no index byte reaches the JSON parser before its signature checks, and no snapshot byte before its hash matches the signed index. It stores the highest accepted `seq`, and each release embeds a `seq` floor, so an old index cannot be replayed to undo a Kill Switch.
- The transport is plain HTTP through a hard-capped client: 10 s deadline, size caps, no redirects, every call guarded. The core gains no TLS stack and no new dynamically linked library.
- Trusted keys are compiled in: the CI signing key (a protected-environment secret in the data repo) and an offline backup key. Rotation ships in a release. Operator-configured keys replace the built-in ones.
- Any failure falls back to the verified cache, then the bundled file. The updater never blocks boot.

Because the client always falls back, an attacker who blocks the connection gets the same outcome as one who withholds updates. TLS cannot prevent blocking, so it would not raise the security floor above what signatures give. It would add a pre-authentication certificate parser into the game process, plus CA upkeep. Upstream dropped HTTPS for packaging reasons: a dynamic OpenSSL dependency made CounterStrikeSharp fail to load on hosts without `libssl.so.1.1` (#929/#930).

## Considered Options

- **Vendored TLS (mbedTLS, BearSSL, wolfSSL) in the core:** a large attack surface reachable before authentication, CA rotation risk, and no gain over signatures while fallback exists.
- **Dynamic system OpenSSL:** the soname varies by host; this is what broke upstream.
- **Steamworks ISteamHTTP:** available only after the first map load; per Valve's docs, certificate verification works only on Windows and macOS.
- **.NET HttpClient, or curl/WinHTTP outside the process:** real TLS, but available only after Bindings resolve (.NET) or not on every host (curl). Kept in the backlog as defence in depth that stages files for native verification.
- **TUF in full (offline root delegating online keys, expiry, snapshot/timestamp roles, mirrors):** these protect against withholding and allow key rotation without a release. The target is "no obvious gaps", not maximum hardening, so they are deferred.
