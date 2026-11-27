#pragma once

// The Gamedata Source (ADR-0006): decides which Gamedata the core boots on, from the signed Gamedata Feed or the bundled
// gamedata.json. Pure: the network comes in through an http::Transport, so tests run it against fakes.
//
// Feed layout, relative to the feed URL:
//   index.json       { "seq": <uint below 2^63>, "issued_at": "<iso time>",
//                      "builds": { "<SourceRevision>": { "patchVersion": "<label>",
//                                                        "<os>": { "file": "<path>", "size": <bytes>, "sha256": "<hex>" } } } }
//   index.json.sig   72 bytes: key id (8) + Ed25519 signature (64) over kIndexSignaturePrefix + index.json's raw bytes
//   <file>           a Gamedata Snapshot: the complete Gamedata for that build and OS; a relative path (the publisher
//                    names it by content, so a published snapshot never changes)
// Signer: the data repo's publisher (cssharp-gamedata publisher/publish.py).
//
// The bundled gamedata.json carries a top-level "sourceRevision": "<SourceRevision>" stamp: the build it was made for.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/binding_resolver.h"

namespace counterstrikesharp::http {
class Transport;
}

namespace counterstrikesharp::feed {

using PublicKey = std::array<std::uint8_t, 32>;

// What an index signature covers ahead of the index bytes: domain separation, so no other signed message passes as an index.
inline constexpr std::string_view kIndexSignaturePrefix{ "cssharp-gamedata-index-v1\0", 26 };

// Ed25519 (RFC 8032, SHA-512) through vendored Monocypher. A signature that is not 64 bytes never verifies.
bool VerifyEd25519(const PublicKey& key, std::string_view message, std::string_view signature);

// The first 8 bytes of sha256(key), as 16 lowercase hex digits.
std::string KeyId(const PublicKey& key);

enum class GamedataSource
{
    Bundled, // the bundled gamedata.json, stamped with this build or not at all
    ExactBuild, // the feed's Gamedata Snapshot for exactly this build and OS, fetched this boot or cached
    // The newest Gamedata of another build, a cached snapshot or the bundled file stamped with it: the feed has none for
    // this build (yet).
    CarriedForward,
};

// Where the boot's Gamedata came from, for the startup report.
struct Provenance
{
    GamedataSource source = GamedataSource::Bundled;
    bool cached = false; // read from the Gamedata cache, not fetched this boot
    std::string build; // the snapshot's build label: this build (ExactBuild) or the one carried forward from; empty for Bundled
    std::optional<std::uint64_t> seq; // the seq of the signed index that vouches for the snapshot (none for the bundled file)
    std::string keyId; // the key that signed that index (empty for the bundled file)
    // The chosen Gamedata's "disabledEngineFeatures" (see bindings::ParseKillSwitches), none forced. Carried forward with
    // the snapshot: they stay in force until this build has its own snapshot.
    std::vector<bindings::EngineFeatureKill> killSwitches;
    std::vector<std::string> warnings; // why the feed was not used, or what was off with it
};

struct GamedataSourceInputs
{
    const http::Transport* transport = nullptr; // null: the feed is off, nothing is fetched
    std::string feedUrl; // "http://host[:port][/path]"
    // The operator's GamedataFeedPublicKeys (64 hex digits each): when set, they replace the built-in keys.
    std::optional<std::vector<std::string>> publicKeys;
    bindings::BuildIdentity build;
    std::string os; // "linux" or "windows"
    std::string bundled; // the bundled gamedata.json's bytes
    // The directory of the bundled gamedata.json. The Gamedata cache lives in its "cache" subdirectory, never as *.json
    // directly in it (managed loads every gamedata/*.json). Empty: no cache and no legacy cleanup.
    std::filesystem::path gamedataDir;
    // The lowest seq accepted even when the cache holds no accepted index (deleted state): the release's seq floor.
    std::uint64_t seqFloor = 0;
    // Legacy updater keys found in the core config (AutoUpdateEnabled, AutoUpdateURL): ignored, one deprecation warning.
    std::vector<std::string> legacyConfigKeys;
};

struct ChosenGamedata
{
    std::string bytes;
    Provenance provenance;
};

// With the feed on, in order:
//   1. the snapshot for exactly this build and OS, cached or fetched, that matches the signed index's size and sha256;
//   2. otherwise the newest by SourceRevision of the cached snapshots that match the signed index and the bundled file
//      (an unstamped bundled file counts as oldest), reported as carried forward from its build;
//   3. otherwise the bundled bytes.
// The bundled file, whichever step picks it, is carried forward from the build it is stamped with when that is not
// this build (with the feed off too), else Bundled.
// The signed index is the fetched one when its seq is not below the cached index's or seqFloor (rollback), else the
// cached one. The reasons for skipping a step are provenance warnings. No index byte is parsed before its signature
// verifies and no snapshot byte before its hash matches, cached files included. Accepted indexes and verified snapshots
// are cached by atomic writes; at most the newest 20 builds stay cached. Never throws: boot never fails because of the
// feed. With the feed off, the bundled bytes, and nothing is fetched or read from the cache.
ChosenGamedata ChooseGamedata(const GamedataSourceInputs& inputs);

// What differs between the Gamedata the server runs on and a newer snapshot for its build, for this OS.
struct GamedataChanges
{
    std::vector<std::string> keys; // Gamedata keys added, removed or changed, sorted
    std::vector<std::string> killSwitches; // Engine Feature ids whose Kill Switch was added, removed or changed, sorted
};

// A newer verified snapshot for the running build, staged in the Gamedata cache: the next boot applies it.
struct StagedSnapshot
{
    std::string build; // the snapshot's build label
    std::uint64_t seq = 0; // the seq of the signed index that vouches for it
    GamedataChanges changes; // against the running Gamedata
};

struct PollResult
{
    std::optional<StagedSnapshot> staged; // only the poll that staged it reports it
    std::string etag; // the index's ETag for the next poll's If-None-Match; empty: fetch it in full next time
    std::vector<std::string> warnings; // why this poll found nothing
};

// One poll while the server runs (gamedata applies at boot only). Asks the feed for its index (conditionally on etag),
// verifies it as ChooseGamedata does (signature, rollback against the cached index and seqFloor) and caches it. When it
// lists a snapshot for this build and OS that is neither the running Gamedata nor staged already, fetches and verifies
// that snapshot and stages it in the cache. Uses inputs' transport, feed, keys, build, os, gamedataDir and seqFloor; does
// nothing without a transport or a gamedataDir (the cache is the record of what was staged). Never throws.
PollResult PollGamedataFeed(const GamedataSourceInputs& inputs, std::string_view running, const std::string& etag);

// One line, e.g. "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata Feed, seq 7, key
// de4a068ed7d94def", "Gamedata: carried forward from build 1.41.9.7 (SourceRevision 11228971) in the Gamedata cache, seq
// 7, key de4a068ed7d94def", "Gamedata: carried forward from build unknown (SourceRevision 11228971) in the bundled
// gamedata.json" (the stamp names no PatchVersion) or "Gamedata: bundled gamedata.json; warnings: index signature
// invalid".
std::string FormatProvenance(const Provenance& provenance);

// The provenance in the Resolution Report (managed reads it as EngineFeatures.Gamedata):
//   { "source": "Bundled"|"ExactBuild"|"CarriedForward", "cached": bool, "build": "<label>" | null (Bundled),
//     "seq": <uint> | null, "keyId": "<hex>" | null, "killSwitches": [ { "id", "reason" } ], "warnings": ["..."],
//     "line": FormatProvenance }
nlohmann::json ProvenanceToJson(const Provenance& provenance);

} // namespace counterstrikesharp::feed
