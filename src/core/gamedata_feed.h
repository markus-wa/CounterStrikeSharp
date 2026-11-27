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
// Signer: eng/gamedata-feed/sign_feed.py.

#include <array>
#include <cstdint>
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
    ExactBuild, // the feed's Gamedata Snapshot for exactly this build and OS
};

// Where the boot's Gamedata came from, for the startup report.
struct Provenance
{
    GamedataSource source = GamedataSource::Bundled;
    std::string build; // the snapshot's build label (ExactBuild only)
    std::optional<std::uint64_t> seq; // the signed index's seq (ExactBuild only)
    std::string keyId; // the key that signed the index (ExactBuild only)
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
};

struct ChosenGamedata
{
    std::string bytes;
    Provenance provenance;
};

// The feed's snapshot for exactly this build and OS, when the signed index lists it and it matches the index's size and
// sha256; otherwise the bundled bytes, with the reason in provenance warnings. No index byte is parsed before its
// signature verifies and no snapshot byte before its hash matches. Never throws: boot never fails because of the feed.
ChosenGamedata ChooseGamedata(const GamedataSourceInputs& inputs);

// One line, e.g. "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata Feed, seq 7, key
// de4a068ed7d94def" or "Gamedata: bundled gamedata.json; warnings: index signature invalid".
std::string FormatProvenance(const Provenance& provenance);

} // namespace counterstrikesharp::feed
