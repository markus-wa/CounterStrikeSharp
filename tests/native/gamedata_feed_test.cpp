#include <doctest.h>

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/gamedata_feed.h"
#include "core/http_client.h"
#include "core/sha256.h"
#include "monocypher-ed25519.h"

using namespace counterstrikesharp;

namespace {

std::string Hex(const std::array<std::uint8_t, 32>& bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const auto byte : bytes)
    {
        hex += digits[byte >> 4];
        hex += digits[byte & 0xF];
    }
    return hex;
}

std::string Unhex(std::string_view hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes += static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16));
    return bytes;
}

feed::PublicKey KeyFromHex(std::string_view hex)
{
    feed::PublicKey key{};
    const auto bytes = Unhex(hex);
    std::copy(bytes.begin(), bytes.end(), key.begin());
    return key;
}

// Signs like eng/gamedata-feed/sign_feed.py: the signature file is key id + Ed25519 signature.
struct Signer
{
    std::uint8_t secret[64]{};
    feed::PublicKey publicKey{};

    explicit Signer(std::string_view seedHex)
    {
        auto seed = Unhex(seedHex);
        crypto_ed25519_key_pair(secret, publicKey.data(), reinterpret_cast<std::uint8_t*>(seed.data()));
    }

    std::string PublicHex() const { return Hex(publicKey); }

    std::string Sign(std::string_view index, bool withPrefix = true) const
    {
        const std::string message = (withPrefix ? std::string(feed::kIndexSignaturePrefix) : std::string()) + std::string(index);
        std::uint8_t signature[64];
        crypto_ed25519_sign(signature, secret, reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
        return Unhex(feed::KeyId(publicKey)) + std::string(reinterpret_cast<const char*>(signature), 64);
    }
};

// The built-in test key (eng/gamedata-feed/test-key.pem) and one the core does not know.
const Signer kBuiltinTestKey("381e97f90febb8217de4ad207eea6cca8f150c2ffeadd0f84a1b6a2ef1897453");
const Signer kOperatorKey("0101010101010101010101010101010101010101010101010101010101010101");

struct FakeTransport final : http::Transport
{
    std::map<std::string, std::string> files;
    mutable std::vector<std::string> requested;

    http::FetchResult Get(const std::string& url) const override
    {
        requested.push_back(url);
        const auto file = files.find(url);
        if (file == files.end()) return { std::nullopt, "404 Not Found" };
        return { file->second, "" };
    }
};

const std::string kFeedUrl = "http://feed.test/gamedata";
const bindings::BuildIdentity kBuild{ "1.42.0.1", "11268945" };
const std::string kSnapshot = R"({ "FromTheFeed": { "offsets": { "linux": 1, "windows": 2 } } })";
const std::string kBundled = R"({ "Bundled": { "offsets": { "linux": 3, "windows": 4 } } })";

std::string IndexFor(const std::string& snapshot, std::string_view sourceRevision = "11268945", std::string_view os = "linux")
{
    nlohmann::json index;
    index["seq"] = 7;
    index["issued_at"] = "2026-11-27T00:00:00Z";
    index["builds"][std::string(sourceRevision)]["patchVersion"] = "1.42.0.1";
    index["builds"][std::string(sourceRevision)][std::string(os)] = {
        { "file", "snapshots/" + std::string(sourceRevision) + "/" + std::string(os) + ".json" },
        { "size", snapshot.size() },
        { "sha256", Hex(Sha256(snapshot)) },
    };
    return index.dump(2);
}

// A feed serving index, its signature and the snapshot, as the publisher lays them out.
FakeTransport Feed(const std::string& index, const std::string& signature, const std::string& snapshot = kSnapshot,
                   std::string_view os = "linux")
{
    FakeTransport transport;
    transport.files[kFeedUrl + "/index.json"] = index;
    transport.files[kFeedUrl + "/index.json.sig"] = signature;
    transport.files[kFeedUrl + "/snapshots/11268945/" + std::string(os) + ".json"] = snapshot;
    return transport;
}

FakeTransport SignedFeed(const Signer& signer = kBuiltinTestKey)
{
    const auto index = IndexFor(kSnapshot);
    return Feed(index, signer.Sign(index));
}

feed::GamedataSourceInputs Inputs(const http::Transport* transport)
{
    feed::GamedataSourceInputs inputs;
    inputs.transport = transport;
    inputs.feedUrl = kFeedUrl;
    inputs.build = kBuild;
    inputs.os = "linux";
    inputs.bundled = kBundled;
    return inputs;
}

bool AnyWarningContains(const feed::Provenance& provenance, std::string_view text)
{
    return std::any_of(provenance.warnings.begin(), provenance.warnings.end(), [&](const std::string& warning) {
        return warning.find(text) != std::string::npos;
    });
}

std::string Flip(std::string bytes, std::size_t at)
{
    bytes[at] ^= 0x01;
    return bytes;
}

} // namespace

// FIPS 180-2 appendix B and the empty message.
TEST_CASE("sha256 matches the FIPS 180-2 test vectors")
{
    CHECK(Hex(Sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Hex(Sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Hex(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Hex(Sha256(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// RFC 8032 section 7.1, tests 1-3.
TEST_CASE("Ed25519 verification passes the RFC 8032 test vectors and rejects altered ones")
{
    struct Vector
    {
        const char* publicKey;
        const char* message;
        const char* signature;
    };
    const Vector vectors[] = {
        { "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
          "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b" },
        { "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
          "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" },
        { "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
          "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a" },
    };
    for (const auto& vector : vectors)
    {
        CAPTURE(vector.publicKey);
        const auto key = KeyFromHex(vector.publicKey);
        const auto message = Unhex(vector.message);
        const auto signature = Unhex(vector.signature);
        CHECK(feed::VerifyEd25519(key, message, signature));
        CHECK_FALSE(feed::VerifyEd25519(key, message + "x", signature));
        CHECK_FALSE(feed::VerifyEd25519(key, message, Flip(signature, 10)));
        CHECK_FALSE(feed::VerifyEd25519(key, message, signature.substr(0, 63)));
    }
}

TEST_CASE("A signed exact-build snapshot is booted on and reported with its build, seq and key")
{
    const auto transport = SignedFeed();
    const auto chosen = feed::ChooseGamedata(Inputs(&transport));

    CHECK(chosen.bytes == kSnapshot);
    CHECK(chosen.provenance.source == feed::GamedataSource::ExactBuild);
    CHECK(chosen.provenance.seq == 7);
    CHECK(chosen.provenance.keyId == "de4a068ed7d94def");
    CHECK(chosen.provenance.warnings.empty());
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata Feed, seq 7, key de4a068ed7d94def");
}

TEST_CASE("A flipped byte in the index, its signature or the snapshot boots on bundled, rejected before parse")
{
    const auto index = IndexFor(kSnapshot);
    const auto signature = kBuiltinTestKey.Sign(index);

    SUBCASE("index: a flip that breaks the JSON is reported as a bad signature, not a parse error")
    {
        const auto transport = Feed(Flip(index, 0), signature);
        const auto chosen = feed::ChooseGamedata(Inputs(&transport));
        CHECK(chosen.bytes == kBundled);
        CHECK(chosen.provenance.source == feed::GamedataSource::Bundled);
        CHECK(AnyWarningContains(chosen.provenance, "index signature invalid"));
        CHECK(transport.requested.size() == 2); // index and signature only: the snapshot is never fetched
    }
    SUBCASE("signature")
    {
        const auto transport = Feed(index, Flip(signature, 20));
        const auto chosen = feed::ChooseGamedata(Inputs(&transport));
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "index signature invalid"));
    }
    SUBCASE("snapshot: a flip that breaks the JSON is reported as a hash mismatch, not a parse error")
    {
        const auto transport = Feed(index, signature, Flip(kSnapshot, 0));
        const auto chosen = feed::ChooseGamedata(Inputs(&transport));
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "does not match the signed index"));
    }
}

TEST_CASE("An index signed by an unknown key boots on bundled")
{
    const auto transport = SignedFeed(kOperatorKey);
    const auto chosen = feed::ChooseGamedata(Inputs(&transport));
    CHECK(chosen.bytes == kBundled);
    CHECK(AnyWarningContains(chosen.provenance, "unknown key " + feed::KeyId(kOperatorKey.publicKey)));
}

TEST_CASE("A signature over the index without the domain-separation prefix is rejected")
{
    const auto index = IndexFor(kSnapshot);
    const auto transport = Feed(index, kBuiltinTestKey.Sign(index, false));
    const auto chosen = feed::ChooseGamedata(Inputs(&transport));
    CHECK(chosen.bytes == kBundled);
    CHECK(AnyWarningContains(chosen.provenance, "index signature invalid"));
}

TEST_CASE("No snapshot for this build boots on bundled with the reason")
{
    const auto index = IndexFor(kSnapshot, "11228971");
    const auto transport = Feed(index, kBuiltinTestKey.Sign(index));
    const auto chosen = feed::ChooseGamedata(Inputs(&transport));
    CHECK(chosen.bytes == kBundled);
    CHECK(FormatProvenance(chosen.provenance) ==
          "Gamedata: bundled gamedata.json; warnings: the Gamedata Feed has no snapshot for build 1.42.0.1 (SourceRevision 11268945) on "
          "linux yet");
}

TEST_CASE("An unknown SourceRevision boots on bundled without asking the feed")
{
    const auto transport = SignedFeed();
    auto inputs = Inputs(&transport);
    inputs.build = bindings::ParseBuildIdentity("");
    const auto chosen = feed::ChooseGamedata(inputs);
    CHECK(chosen.bytes == kBundled);
    CHECK(AnyWarningContains(chosen.provenance, "SourceRevision is unknown"));
    CHECK(transport.requested.empty());
}

TEST_CASE("An unreachable feed or a malformed signed index boots on bundled with a warning")
{
    SUBCASE("unreachable")
    {
        const FakeTransport transport;
        const auto chosen = feed::ChooseGamedata(Inputs(&transport));
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "Gamedata Feed unreachable: http://feed.test/gamedata/index.json: 404 Not Found"));
    }
    SUBCASE("signed, but not an index")
    {
        const std::string malformed = R"({ "seq": "seven" })";
        const auto transport = Feed(malformed, kBuiltinTestKey.Sign(malformed));
        const auto chosen = feed::ChooseGamedata(Inputs(&transport));
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "index unreadable"));
    }
}

TEST_CASE("With the feed off nothing is fetched and bundled is used without warnings")
{
    const auto chosen = feed::ChooseGamedata(Inputs(nullptr));
    CHECK(chosen.bytes == kBundled);
    CHECK(chosen.provenance.warnings.empty());
    CHECK(FormatProvenance(chosen.provenance) == "Gamedata: bundled gamedata.json");
}

TEST_CASE("Operator-set GamedataFeedPublicKeys replace the built-in keys")
{
    SUBCASE("an index signed by the operator's key is accepted")
    {
        const auto transport = SignedFeed(kOperatorKey);
        auto inputs = Inputs(&transport);
        inputs.publicKeys = std::vector<std::string>{ kOperatorKey.PublicHex() };
        const auto chosen = feed::ChooseGamedata(inputs);
        CHECK(chosen.bytes == kSnapshot);
        CHECK(chosen.provenance.keyId == feed::KeyId(kOperatorKey.publicKey));
    }
    SUBCASE("the built-in key no longer is")
    {
        const auto transport = SignedFeed(kBuiltinTestKey);
        auto inputs = Inputs(&transport);
        inputs.publicKeys = std::vector<std::string>{ kOperatorKey.PublicHex() };
        const auto chosen = feed::ChooseGamedata(inputs);
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "unknown key de4a068ed7d94def"));
    }
    SUBCASE("an entry that is not a key is ignored with a warning")
    {
        const auto transport = SignedFeed(kOperatorKey);
        auto inputs = Inputs(&transport);
        inputs.publicKeys = std::vector<std::string>{ "not-a-key", kOperatorKey.PublicHex() };
        const auto chosen = feed::ChooseGamedata(inputs);
        CHECK(chosen.bytes == kSnapshot);
        CHECK(AnyWarningContains(chosen.provenance, "\"not-a-key\" is not an Ed25519 public key"));
    }
}

// Windows runs the same code; only the OS key differs (no Windows native tests in CI).
TEST_CASE("On Windows the index's windows entry is picked")
{
    const auto index = IndexFor(kSnapshot, "11268945", "windows");
    const auto transport = Feed(index, kBuiltinTestKey.Sign(index), kSnapshot, "windows");
    auto inputs = Inputs(&transport);
    inputs.os = "windows";
    CHECK(feed::ChooseGamedata(inputs).bytes == kSnapshot);
    inputs.os = "linux";
    CHECK(feed::ChooseGamedata(inputs).bytes == kBundled);
}

namespace {

std::string ReadFixture(const std::string& relative)
{
    std::ifstream stream(std::string(CSSHARP_FEED_FIXTURES) + "/" + relative, std::ios::binary);
    REQUIRE(stream);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

} // namespace

// Cross-implementation: tests/native/fixtures/gamedata-feed was written by the Python signer (eng/gamedata-feed/sign_feed.py
// with test-key.pem); eng/gamedata-feed/test_sign_feed.py checks it still produces exactly these bytes.
TEST_CASE("A feed signed by the Python signer verifies natively, and a mutated one does not")
{
    const auto index = ReadFixture("index.json");
    const auto signature = ReadFixture("index.json.sig");
    const auto snapshot = ReadFixture("snapshots/11268945/linux.json");

    const auto published = Feed(index, signature, snapshot);
    const auto chosen = feed::ChooseGamedata(Inputs(&published));
    CHECK(chosen.bytes == snapshot);
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata Feed, seq 3, key de4a068ed7d94def");

    const std::pair<const char*, FakeTransport> mutations[] = {
        { "index", Feed(Flip(index, index.size() / 2), signature, snapshot) },
        { "signature", Feed(index, Flip(signature, 40), snapshot) },
        { "snapshot", Feed(index, signature, Flip(snapshot, 5)) },
    };
    for (const auto& [name, mutated] : mutations)
    {
        CAPTURE(name);
        CHECK(feed::ChooseGamedata(Inputs(&mutated)).bytes == kBundled);
    }
}
