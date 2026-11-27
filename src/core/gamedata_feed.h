#pragma once

// The Gamedata Source (ADR-0006): decides which Gamedata the core boots on, from the signed Gamedata Feed or the bundled
// gamedata.json. Pure: the network comes in through an http::Transport, so tests run it against fakes.
//
// Feed layout, relative to the feed URL:
//   index.json       { "seq": <uint>, "issued_at": "<iso time>",
//                      "builds": { "<SourceRevision>": { "patchVersion": "<label>",
//                                                        "<os>": { "file": "<path>", "size": <bytes>, "sha256": "<hex>" } } } }
//   index.json.sig   72 bytes: key id (8) + Ed25519 signature (64) over kIndexSignaturePrefix + index.json's raw bytes
//   <file>           a Gamedata Snapshot: the complete Gamedata for that build and OS
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
    Bundled, // the bundled gamedata.json
    ExactBuild, // the feed's Gamedata Snapshot for exactly this build and OS, fetched this boot or cached
    CarriedForward, // the newest cached snapshot of another build: the feed has none for this build (yet)
};

// Where the boot's Gamedata came from, for the startup report.
struct Provenance
{
    GamedataSource source = GamedataSource::Bundled;
    bool cached = false; // read from the Gamedata cache, not fetched this boot (always for CarriedForward)
    std::string build; // the snapshot's build label (not Bundled)
    std::optional<std::uint64_t> seq; // the seq of the signed index that vouches for the snapshot (not Bundled)
    std::string keyId; // the key that signed that index (not Bundled)
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
//      (an unstamped bundled file counts as oldest): a cached one is reported as carried forward;
//   3. otherwise the bundled bytes.
// The signed index is the fetched one when its seq is not below the cached index's or seqFloor (rollback), else the
// cached one. The reasons for skipping a step are provenance warnings. No index byte is parsed before its signature
// verifies and no snapshot byte before its hash matches, cached files included. Accepted indexes and verified snapshots
// are cached by atomic writes; at most the newest 20 builds stay cached. Never throws: boot never fails because of the
// feed. With the feed off, the bundled bytes, and nothing is fetched or read from the cache.
ChosenGamedata ChooseGamedata(const GamedataSourceInputs& inputs);

// One line, e.g. "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata Feed, seq 7, key
// de4a068ed7d94def", "Gamedata: carried forward from build 1.41.9.7 (SourceRevision 11228971) in the Gamedata cache, seq
// 7, key de4a068ed7d94def" or "Gamedata: bundled gamedata.json; warnings: index signature invalid".
std::string FormatProvenance(const Provenance& provenance);

} // namespace counterstrikesharp::feed
