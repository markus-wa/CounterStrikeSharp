#include "core/gamedata_feed.h"

#include <algorithm>
#include <exception>

#include <nlohmann/json.hpp>

#include "core/http_client.h"
#include "core/sha256.h"
#include "monocypher-ed25519.h"

namespace counterstrikesharp::feed {
namespace {

// shortcut: the only built-in key is the test key (cssharp-gamedata publisher/test-key.pem, private key public), so
// anyone can sign a feed the core trusts; ticket 09 replaces it with the CI signing key and the offline backup key.
constexpr std::string_view kBuiltinKeys[] = {
    "7a8dad7c818602e6381074af43f1bf44903323425627485d51125e2db4f28b5b",
};

constexpr std::size_t kKeyIdSize = 8;
constexpr std::size_t kSignatureSize = 64;

std::string ToHex(const std::uint8_t* bytes, std::size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (std::size_t i = 0; i < size; ++i)
    {
        hex += digits[bytes[i] >> 4];
        hex += digits[bytes[i] & 0xF];
    }
    return hex;
}

std::optional<PublicKey> ParseKey(std::string_view hex)
{
    if (hex.size() != 64) return std::nullopt;
    PublicKey key{};
    for (std::size_t i = 0; i < key.size(); ++i)
    {
        int value = 0;
        for (const char c : hex.substr(2 * i, 2))
        {
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) return std::nullopt;
            value = value * 16 + digit;
        }
        key[i] = static_cast<std::uint8_t>(value);
    }
    return key;
}

// The operator's keys replace the built-in ones when set; entries that are not keys are skipped with a warning.
std::vector<PublicKey> TrustedKeys(const std::optional<std::vector<std::string>>& configured, std::vector<std::string>& warnings)
{
    std::vector<PublicKey> keys;
    if (!configured)
    {
        for (const auto hex : kBuiltinKeys)
            keys.push_back(*ParseKey(hex));
        return keys;
    }
    for (const auto& hex : *configured)
    {
        if (const auto key = ParseKey(hex)) keys.push_back(*key);
        else
            warnings.push_back("GamedataFeedPublicKeys entry \"" + hex + "\" is not an Ed25519 public key (64 hex digits), ignored");
    }
    return keys;
}

// The signing key's id when signature is a valid signature file for index by one of keys; otherwise the reason.
bool VerifyIndex(std::string_view index, std::string_view signature, const std::vector<PublicKey>& keys, std::string& keyId,
                 std::string& error)
{
    if (signature.size() != kKeyIdSize + kSignatureSize)
    {
        error = "index signature file has " + std::to_string(signature.size()) + " bytes, expected " +
                std::to_string(kKeyIdSize + kSignatureSize);
        return false;
    }
    keyId = ToHex(reinterpret_cast<const std::uint8_t*>(signature.data()), kKeyIdSize);
    const auto key = std::find_if(keys.begin(), keys.end(), [&](const PublicKey& candidate) {
        return KeyId(candidate) == keyId;
    });
    if (key == keys.end())
    {
        error = "index signed by unknown key " + keyId;
        return false;
    }
    if (!VerifyEd25519(*key, std::string(kIndexSignaturePrefix) + std::string(index), signature.substr(kKeyIdSize)))
    {
        error = "index signature invalid (key " + keyId + ")";
        return false;
    }
    return true;
}

std::string Fetch(const http::Transport& transport, const std::string& url, std::string& error)
{
    auto result = transport.Get(url);
    if (!result.body) error = "Gamedata Feed unreachable: " + url + ": " + result.error;
    return result.body.value_or("");
}

// The verified snapshot for the build, described in provenance, or none with the reason in error.
std::optional<std::string> FromFeed(const GamedataSourceInputs& inputs, Provenance& provenance, std::string& error)
{
    if (inputs.build.sourceRevision == "unknown")
    {
        error = "this build's SourceRevision is unknown (no steam.inf), no snapshot to pick";
        return std::nullopt;
    }

    std::string base = inputs.feedUrl;
    while (!base.empty() && base.back() == '/')
        base.pop_back();

    const auto keys = TrustedKeys(inputs.publicKeys, provenance.warnings);
    const auto index = Fetch(*inputs.transport, base + "/index.json", error);
    if (!error.empty()) return std::nullopt;
    const auto signature = Fetch(*inputs.transport, base + "/index.json.sig", error);
    if (!error.empty()) return std::nullopt;
    if (!VerifyIndex(index, signature, keys, provenance.keyId, error)) return std::nullopt;

    // Verified: from here on the index is the publisher's.
    const auto root = nlohmann::json::parse(index);
    const auto& builds = root.at("builds");
    const auto build = builds.find(inputs.build.sourceRevision);
    if (build == builds.end() || !build->contains(inputs.os))
    {
        error = "the Gamedata Feed has no snapshot for build " + inputs.build.Label() + " on " + inputs.os + " yet";
        return std::nullopt;
    }
    const auto& entry = build->at(inputs.os);
    const auto file = entry.at("file").get<std::string>();
    const auto size = entry.at("size").get<std::uint64_t>();
    const auto sha256 = entry.at("sha256").get<std::string>();

    auto snapshot = Fetch(*inputs.transport, base + "/" + file, error);
    if (!error.empty()) return std::nullopt;
    const auto digest = Sha256(snapshot);
    if (snapshot.size() != size || ToHex(digest.data(), digest.size()) != sha256)
    {
        error = "snapshot " + file + " does not match the signed index (size " + std::to_string(snapshot.size()) + ", index says " +
                std::to_string(size) + ", or sha256 differs)";
        return std::nullopt;
    }
    // Hash verified; still Gamedata the resolver can read, or the bundled file is the better choice.
    try
    {
        bindings::ParseGamedataBytes(snapshot, inputs.os);
    }
    catch (const std::exception& ex)
    {
        error = "snapshot " + file + " is not readable Gamedata: " + ex.what();
        return std::nullopt;
    }

    provenance.source = GamedataSource::ExactBuild;
    provenance.build = inputs.build.Label();
    provenance.seq = root.at("seq").get<std::uint64_t>();
    return snapshot;
}

} // namespace

bool VerifyEd25519(const PublicKey& key, std::string_view message, std::string_view signature)
{
    if (signature.size() != kSignatureSize) return false;
    return crypto_ed25519_check(reinterpret_cast<const std::uint8_t*>(signature.data()), key.data(),
                                reinterpret_cast<const std::uint8_t*>(message.data()), message.size()) == 0;
}

std::string KeyId(const PublicKey& key)
{
    const auto digest = Sha256(std::string_view(reinterpret_cast<const char*>(key.data()), key.size()));
    return ToHex(digest.data(), kKeyIdSize);
}

ChosenGamedata ChooseGamedata(const GamedataSourceInputs& inputs)
{
    if (inputs.transport == nullptr) return { inputs.bundled, {} };

    Provenance provenance;
    std::string error;
    try
    {
        if (auto snapshot = FromFeed(inputs, provenance, error)) return { std::move(*snapshot), std::move(provenance) };
    }
    catch (const std::exception& ex)
    {
        error = std::string("Gamedata Feed index unreadable: ") + ex.what();
    }
    ChosenGamedata bundled{ inputs.bundled, {} };
    bundled.provenance.warnings = std::move(provenance.warnings);
    bundled.provenance.warnings.push_back(std::move(error));
    return bundled;
}

std::string FormatProvenance(const Provenance& provenance)
{
    std::string text = provenance.source == GamedataSource::ExactBuild
                           ? "Gamedata: exact build " + provenance.build + " from the Gamedata Feed, seq " +
                                 std::to_string(provenance.seq.value_or(0)) + ", key " + provenance.keyId
                           : "Gamedata: bundled gamedata.json";
    for (std::size_t i = 0; i < provenance.warnings.size(); ++i)
        text += (i == 0 ? "; warnings: " : "; ") + provenance.warnings[i];
    return text;
}

} // namespace counterstrikesharp::feed
