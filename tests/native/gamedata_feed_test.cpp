#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
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

// Signs like the data repo's publisher (cssharp-gamedata publisher/publish.py): key id + Ed25519 signature.
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

// The built-in test key (cssharp-gamedata publisher/test-key.pem) and one the core does not know.
const Signer kBuiltinTestKey("381e97f90febb8217de4ad207eea6cca8f150c2ffeadd0f84a1b6a2ef1897453");
const Signer kOperatorKey("0101010101010101010101010101010101010101010101010101010101010101");

struct FakeTransport final : http::Transport
{
    std::map<std::string, std::string> files;
    mutable std::vector<std::string> requested;

    http::FetchResult Get(const http::Request& request) const override
    {
        requested.push_back(request.url);
        const auto file = files.find(request.url);
        if (file == files.end()) return { std::nullopt, "404 Not Found" };
        if (file->second.size() > request.maxBytes) return { std::nullopt, "over the cap" };
        // The ETag is the body itself: unchanged exactly when the body is.
        if (request.ifNoneMatch == file->second) return { std::nullopt, "", file->second, true };
        return { file->second, "", file->second };
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

// seq stays below 2^63, so it fits every signed 64-bit integer that may hold it (the managed provenance, JSON readers).
TEST_CASE("A signed index with a seq of 2^63 or more is rejected")
{
    const auto withSeq = [](const std::string& seq) {
        auto index = IndexFor(kSnapshot);
        index.replace(index.find("\"seq\": 7"), 8, "\"seq\": " + seq);
        return Feed(index, kBuiltinTestKey.Sign(index));
    };

    const auto largest = withSeq("9223372036854775807");
    CHECK(feed::ChooseGamedata(Inputs(&largest)).provenance.seq == 9223372036854775807ull);

    const auto tooLarge = withSeq("9223372036854775808");
    const auto chosen = feed::ChooseGamedata(Inputs(&tooLarge));
    CHECK(chosen.bytes == kBundled);
    CHECK(AnyWarningContains(chosen.provenance, "index unreadable: seq 9223372036854775808 is not below 2^63"));
}

// Published snapshots are content-addressed: each edit gets a new file name, whatever the index names is fetched.
TEST_CASE("The snapshot is fetched from the path the signed index names, which must stay inside the feed")
{
    const auto withFile = [](const std::string& file) {
        auto root = nlohmann::json::parse(IndexFor(kSnapshot));
        root["builds"]["11268945"]["linux"]["file"] = file;
        const auto index = root.dump(2);
        auto transport = Feed(index, kBuiltinTestKey.Sign(index));
        transport.files[kFeedUrl + "/" + file] = kSnapshot;
        return transport;
    };

    const auto hashed = withFile("snapshots/11268945/linux.0123456789abcdef.json");
    CHECK(feed::ChooseGamedata(Inputs(&hashed)).bytes == kSnapshot);

    for (const std::string file : { "../snapshots/11268945/linux.json", "snapshots/../../linux.json", "/snapshots/11268945/linux.json",
                                    "http://evil.test/linux.json", "snapshots//linux.json", "snapshots\\11268945\\linux.json", "" })
    {
        CAPTURE(file);
        const auto escaping = withFile(file);
        const auto chosen = feed::ChooseGamedata(Inputs(&escaping));
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "snapshot path \"" + file + "\" in the signed index is not a relative path, refused"));
    }
}

// The caps are the spec's (index 64 KiB, signature 256 B, snapshot 1 MiB), the same on both OSes.
TEST_CASE("A feed file over its download cap boots on bundled with a warning, on Linux and Windows")
{
    for (const char* os : { "linux", "windows" })
    {
        CAPTURE(os);
        const auto index = IndexFor(kSnapshot, "11268945", os);
        const auto signature = kBuiltinTestKey.Sign(index);
        const auto check = [&](const FakeTransport& transport, const std::string& file) {
            auto inputs = Inputs(&transport);
            inputs.os = os;
            const auto chosen = feed::ChooseGamedata(inputs);
            CHECK(chosen.bytes == kBundled);
            CHECK(AnyWarningContains(chosen.provenance, kFeedUrl + "/" + file + ": over the cap"));
        };
        check(Feed(std::string(64 * 1024 + 1, ' '), signature, kSnapshot, os), "index.json");
        check(Feed(index, std::string(257, 's'), kSnapshot, os), "index.json.sig");
        check(Feed(index, signature, std::string(1024 * 1024 + 1, ' '), os), "snapshots/11268945/" + std::string(os) + ".json");
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

// Cross-implementation: tests/native/fixtures/gamedata-feed is a copy of the data repo's publisher/fixtures/feed, written
// by its publisher with the test key; the publisher's tests check it still produces exactly these bytes (regenerate: data
// repo README, "Cross-implementation fixtures"). `eng/gamedata/bundle.py check` fails CI when this copy differs from it.
TEST_CASE("A feed signed by the Python publisher verifies natively, and a mutated one does not")
{
    const auto index = ReadFixture("index.json");
    const auto signature = ReadFixture("index.json.sig");
    const auto file = nlohmann::json::parse(index)["builds"]["11268945"]["linux"]["file"].get<std::string>();
    const auto snapshot = ReadFixture(file);
    const auto Published = [&](const std::string& index, const std::string& signature, const std::string& snapshot) {
        FakeTransport transport;
        transport.files[kFeedUrl + "/index.json"] = index;
        transport.files[kFeedUrl + "/index.json.sig"] = signature;
        transport.files[kFeedUrl + "/" + file] = snapshot;
        return transport;
    };

    const auto published = Published(index, signature, snapshot);
    const auto chosen = feed::ChooseGamedata(Inputs(&published));
    CHECK(chosen.bytes == snapshot);
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata Feed, seq 1, key de4a068ed7d94def");

    const std::pair<const char*, FakeTransport> mutations[] = {
        { "index", Published(Flip(index, index.size() / 2), signature, snapshot) },
        { "signature", Published(index, Flip(signature, 40), snapshot) },
        { "snapshot", Published(index, signature, Flip(snapshot, 5)) },
    };
    for (const auto& [name, mutated] : mutations)
    {
        CAPTURE(name);
        CHECK(feed::ChooseGamedata(Inputs(&mutated)).bytes == kBundled);
    }
}

// Cache, carry-forward and rollback: boots share a Gamedata directory, as successive server starts do.
namespace {

struct GamedataDir
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("cssharp-gamedata-test-" + std::to_string(std::random_device{}()));

    GamedataDir() { std::filesystem::create_directories(path); }
    ~GamedataDir() { std::filesystem::remove_all(path); }
};

feed::GamedataSourceInputs Inputs(const http::Transport* transport, const GamedataDir& dir)
{
    auto inputs = Inputs(transport);
    inputs.gamedataDir = dir.path;
    return inputs;
}

const FakeTransport kFeedDown;

std::string SnapshotOf(const std::string& revision) { return R"({ "Build)" + revision + R"(": { "offsets": { "linux": 1 } } })"; }

// A feed signed with the built-in key at seq, listing for each SourceRevision a linux snapshot (SnapshotOf by default).
FakeTransport FeedOf(std::uint64_t seq, const std::vector<std::string>& revisions, const std::map<std::string, std::string>& snapshots = {})
{
    nlohmann::json index;
    index["seq"] = seq;
    index["issued_at"] = "2026-11-27T00:00:00Z";
    index["builds"] = nlohmann::json::object();
    FakeTransport transport;
    for (const auto& revision : revisions)
    {
        const auto snapshot = snapshots.contains(revision) ? snapshots.at(revision) : SnapshotOf(revision);
        const auto file = "snapshots/" + revision + "/linux.json";
        index["builds"][revision]["patchVersion"] = "1.0." + revision;
        index["builds"][revision]["linux"] = { { "file", file }, { "size", snapshot.size() }, { "sha256", Hex(Sha256(snapshot)) } };
        transport.files[kFeedUrl + "/" + file] = snapshot;
    }
    const auto bytes = index.dump(2);
    transport.files[kFeedUrl + "/index.json"] = bytes;
    transport.files[kFeedUrl + "/index.json.sig"] = kBuiltinTestKey.Sign(bytes);
    return transport;
}

// One server start on build `revision` sharing dir with earlier ones.
feed::ChosenGamedata Boot(const http::Transport& transport, const GamedataDir& dir, const std::string& revision,
                          const std::string& bundled = kBundled, std::uint64_t seqFloor = 0)
{
    auto inputs = Inputs(&transport, dir);
    inputs.build = { "1.0." + revision, revision };
    inputs.bundled = bundled;
    inputs.seqFloor = seqFloor;
    return feed::ChooseGamedata(inputs);
}

std::string Stamped(const std::string& revision)
{
    return R"({ "sourceRevision": ")" + revision + R"(", "Bundled": { "offsets": { "linux": 3 } } })";
}

// Flips one byte of every file under dir that contains needle, as disk corruption or a hand edit would.
void TamperFilesContaining(const GamedataDir& dir, const std::string& needle)
{
    for (const auto& file : std::filesystem::recursive_directory_iterator(dir.path))
    {
        if (!file.is_regular_file()) continue;
        std::string bytes;
        {
            std::ifstream in(file.path(), std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (bytes.find(needle) == std::string::npos) continue;
        std::ofstream(file.path(), std::ios::binary | std::ios::trunc) << Flip(bytes, bytes.size() - 3);
    }
}

} // namespace

TEST_CASE("With the feed down, the cached snapshot for exactly this build is booted on and reported as cache")
{
    const GamedataDir dir;
    const auto feed = SignedFeed();
    REQUIRE(feed::ChooseGamedata(Inputs(&feed, dir)).provenance.source == feed::GamedataSource::ExactBuild);

    const auto chosen = feed::ChooseGamedata(Inputs(&kFeedDown, dir));
    CHECK(chosen.bytes == kSnapshot);
    CHECK(chosen.provenance.source == feed::GamedataSource::ExactBuild);
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: exact build 1.42.0.1 (SourceRevision 11268945) from the Gamedata cache, seq 7, key de4a068ed7d94def; warnings: "
          "Gamedata Feed unreachable: http://feed.test/gamedata/index.json: 404 Not Found");
}

TEST_CASE("The snapshot for exactly this build beats a newer cached build")
{
    const GamedataDir dir;
    const auto feed = FeedOf(1, { "100", "200", "300" });
    Boot(feed, dir, "300");

    const auto chosen = Boot(feed, dir, "200");
    CHECK(chosen.bytes == SnapshotOf("200"));
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: exact build 1.0.200 (SourceRevision 200) from the Gamedata Feed, seq 1, key de4a068ed7d94def");
}

TEST_CASE("With no snapshot for this build, the newest of the cached snapshots and the bundled file is carried forward")
{
    const GamedataDir dir;
    const auto feed = FeedOf(1, { "100", "150" });
    Boot(feed, dir, "100");
    Boot(feed, dir, "150");

    SUBCASE("a newer cached snapshot beats a bundled file without a stamp, which counts as oldest")
    {
        const auto chosen = Boot(feed, dir, "200");
        CHECK(chosen.bytes == SnapshotOf("150"));
        CHECK(chosen.provenance.source == feed::GamedataSource::CarriedForward);
        CHECK(feed::FormatProvenance(chosen.provenance) ==
              "Gamedata: carried forward from build 1.0.150 (SourceRevision 150) in the Gamedata cache, seq 1, key de4a068ed7d94def; "
              "warnings: the Gamedata Feed has no snapshot for build 1.0.200 (SourceRevision 200) on linux yet");
    }
    SUBCASE("a newer cached snapshot beats an older stamped bundled file")
    {
        CHECK(Boot(feed, dir, "200", Stamped("120")).bytes == SnapshotOf("150"));
    }
    SUBCASE("a newer bundled file beats an older cached snapshot, and is carried forward from its build")
    {
        const auto chosen = Boot(feed, dir, "200", Stamped("180"));
        CHECK(chosen.bytes == Stamped("180"));
        CHECK(chosen.provenance.source == feed::GamedataSource::CarriedForward);
        CHECK_FALSE(chosen.provenance.cached);
        CHECK(feed::FormatProvenance(chosen.provenance) ==
              "Gamedata: carried forward from build unknown (SourceRevision 180) in the bundled gamedata.json; "
              "warnings: the Gamedata Feed has no snapshot for build 1.0.200 (SourceRevision 200) on linux yet");
    }
    SUBCASE("with the feed down too")
    {
        CHECK(Boot(kFeedDown, dir, "200").bytes == SnapshotOf("150"));
    }
}

TEST_CASE("The bundled file is carried forward from the build it is stamped with, unless that is this build or it has no stamp")
{
    const auto bundled = [](const std::string& bytes, const std::string& revision) {
        auto inputs = Inputs(nullptr);
        inputs.build = { "1.0." + revision, revision };
        inputs.bundled = bytes;
        return feed::ChooseGamedata(inputs).provenance;
    };

    const auto other = bundled(Stamped("180"), "200");
    CHECK(other.source == feed::GamedataSource::CarriedForward);
    CHECK(other.build == "unknown (SourceRevision 180)");
    CHECK_FALSE(other.seq);
    CHECK(feed::FormatProvenance(other) == "Gamedata: carried forward from build unknown (SourceRevision 180) in the bundled gamedata.json");

    CHECK(bundled(Stamped("200"), "200").source == feed::GamedataSource::Bundled);
    CHECK(bundled(kBundled, "200").source == feed::GamedataSource::Bundled);
    CHECK(bundled(Stamped("180"), "unknown").source == feed::GamedataSource::Bundled);
}

TEST_CASE("Kill Switches carry forward: a kill in build X stays in force on build Y until Y has its own snapshot")
{
    const GamedataDir dir;
    const std::string killed =
        R"({ "disabledEngineFeatures": [ { "id": "cssharp.server/game-system-events", "reason": "crashes" } ], "Build100": {} })";
    const auto onlyX = FeedOf(1, { "100" }, { { "100", killed } });

    const auto onX = Boot(onlyX, dir, "100").provenance;
    REQUIRE(onX.killSwitches.size() == 1);
    CHECK(onX.source == feed::GamedataSource::ExactBuild);

    // The report names the source build of a carried-forward kill through the provenance.
    const auto onY = Boot(onlyX, dir, "200").provenance;
    REQUIRE(onY.killSwitches.size() == 1);
    CHECK(onY.killSwitches[0].id == "cssharp.server/game-system-events");
    CHECK(onY.killSwitches[0].reason == "crashes");
    CHECK(onY.source == feed::GamedataSource::CarriedForward);
    CHECK(onY.build == "1.0.100 (SourceRevision 100)");

    const auto published = FeedOf(2, { "100", "200" }, { { "100", killed } });
    CHECK(Boot(published, dir, "200").provenance.killSwitches.empty());

    // Also from a bundled file stamped with X.
    const auto stampedX = R"({ "sourceRevision": "100", )" + killed.substr(1);
    const auto fromBundled = Boot(kFeedDown, GamedataDir(), "300", stampedX).provenance;
    REQUIRE(fromBundled.killSwitches.size() == 1);
    CHECK(fromBundled.source == feed::GamedataSource::CarriedForward);
    CHECK(fromBundled.build == "unknown (SourceRevision 100)");
}

TEST_CASE("The bundled gamedata.json is stamped with its build, so it beats a cached older build")
{
    std::ifstream stream(CSSHARP_GAMEDATA_PATH, std::ios::binary);
    const std::string bundled{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    const GamedataDir dir;
    const auto feed = FeedOf(1, { "11228971" }); // 1.41.9.7
    Boot(feed, dir, "11228971");

    CHECK(Boot(feed, dir, "99999999", bundled).bytes == bundled);
}

TEST_CASE("A tampered cache file is rejected: re-fetched when the feed answers, skipped when it does not")
{
    const GamedataDir dir;
    const auto feed = SignedFeed();
    feed::ChooseGamedata(Inputs(&feed, dir));
    TamperFilesContaining(dir, kSnapshot);

    SUBCASE("feed up: fetched again, and the cache is repaired")
    {
        const auto chosen = feed::ChooseGamedata(Inputs(&feed, dir));
        CHECK(chosen.bytes == kSnapshot);
        CHECK_FALSE(chosen.provenance.cached);
        CHECK(AnyWarningContains(chosen.provenance, "cached snapshot for build 1.42.0.1 (SourceRevision 11268945) does not match the signed index"));

        const auto next = feed::ChooseGamedata(Inputs(&kFeedDown, dir));
        CHECK(next.bytes == kSnapshot);
        CHECK(next.provenance.cached);
    }
    SUBCASE("feed down: bundled")
    {
        const auto chosen = feed::ChooseGamedata(Inputs(&kFeedDown, dir));
        CHECK(chosen.bytes == kBundled);
        CHECK(AnyWarningContains(chosen.provenance, "does not match the signed index"));
    }
}

TEST_CASE("A tampered cached index is ignored")
{
    const GamedataDir dir;
    const auto feed = SignedFeed();
    feed::ChooseGamedata(Inputs(&feed, dir));
    TamperFilesContaining(dir, "\"seq\"");

    const auto chosen = feed::ChooseGamedata(Inputs(&kFeedDown, dir));
    CHECK(chosen.bytes == kBundled);
    CHECK(AnyWarningContains(chosen.provenance, "cached index signature invalid (key de4a068ed7d94def), ignored"));
}

TEST_CASE("The cache keeps the newest 20 builds")
{
    const GamedataDir dir;
    std::vector<std::string> revisions;
    for (int build = 1; build <= 21; ++build)
        revisions.push_back(std::to_string(build));
    const auto feed = FeedOf(1, revisions);
    for (const auto& revision : revisions)
        REQUIRE(Boot(feed, dir, revision).provenance.source == feed::GamedataSource::ExactBuild);

    // Pruned at this boot: build 1 is the oldest of 21.
    const auto oldest = Boot(kFeedDown, dir, "1");
    CHECK(oldest.provenance.source == feed::GamedataSource::CarriedForward);
    CHECK(oldest.bytes == SnapshotOf("21"));
    // Kept.
    CHECK(Boot(kFeedDown, dir, "2").provenance.source == feed::GamedataSource::ExactBuild);
}

// Atomicity is a property of how cache files are written, so these name the cache layout: <dir>/cache/<rev>.<os>.json.
TEST_CASE("An interrupted cache write leaves no partial file")
{
    const GamedataDir dir;
    const auto cache = dir.path / "cache";
    const auto snapshot = cache / "100.linux.json";
    const auto feed = FeedOf(1, { "100" });
    const auto TempFiles = [&] {
        std::vector<std::string> temps;
        for (const auto& file : std::filesystem::recursive_directory_iterator(cache))
            if (file.path().filename().string() != "100.linux.json" && file.path().filename().string() != "index.signed")
                temps.push_back(file.path().filename().string());
        return temps;
    };

    SUBCASE("a write that fails before the rename removes its temp file, and the boot goes on")
    {
        std::filesystem::create_directories(snapshot); // the rename onto it fails
        const auto chosen = Boot(feed, dir, "100");
        CHECK(chosen.bytes == SnapshotOf("100"));
        CHECK(AnyWarningContains(chosen.provenance, "could not write the Gamedata cache file"));
        CHECK(TempFiles().empty());
    }
    SUBCASE("a temp file left by a crash is never booted on and is removed at the next boot")
    {
        Boot(feed, dir, "100");
        std::filesystem::remove(snapshot);
        std::ofstream(std::filesystem::path(snapshot) += ".tmp", std::ios::binary) << SnapshotOf("100").substr(0, 10);

        CHECK(Boot(kFeedDown, dir, "100").bytes == kBundled);
        CHECK(TempFiles().empty());
    }
}

TEST_CASE("Rollback: an index with a lower seq than the highest accepted one is rejected")
{
    const GamedataDir dir;
    const auto current = FeedOf(7, { "100" });
    Boot(current, dir, "100");

    const auto rolledBack = FeedOf(5, { "100" }, { { "100", R"({ "Old": {} })" } });
    const auto chosen = Boot(rolledBack, dir, "100");
    CHECK(chosen.bytes == SnapshotOf("100"));
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: exact build 1.0.100 (SourceRevision 100) from the Gamedata cache, seq 7, key de4a068ed7d94def; warnings: Gamedata "
          "Feed index seq 5 is below the highest accepted seq 7, rejected as a rollback");
    // Still rejected next time: the rolled-back index did not replace the accepted one.
    CHECK(Boot(rolledBack, dir, "100").provenance.seq == 7);
}

TEST_CASE("Rollback: an index below the release's seq floor is rejected after the cache state is deleted")
{
    const GamedataDir dir;
    Boot(FeedOf(12, { "100" }), dir, "100", kBundled, 10);
    std::filesystem::remove_all(dir.path);
    std::filesystem::create_directories(dir.path);

    const auto belowFloor = FeedOf(7, { "100" });
    const auto chosen = Boot(belowFloor, dir, "100", kBundled, 10);
    CHECK(chosen.bytes == kBundled);
    CHECK(AnyWarningContains(chosen.provenance, "Gamedata Feed index seq 7 is below the highest accepted seq 10, rejected as a rollback"));

    CHECK(Boot(FeedOf(10, { "100" }), dir, "100", kBundled, 10).provenance.seq == 10);
}

TEST_CASE("Legacy updater leftovers: gamedata.etag is deleted, and AutoUpdate* config keys get one deprecation warning")
{
    const GamedataDir dir;
    std::ofstream(dir.path / "gamedata.etag") << "\"abc\"";
    auto inputs = Inputs(nullptr, dir);
    inputs.legacyConfigKeys = { "AutoUpdateEnabled", "AutoUpdateURL" };

    const auto chosen = feed::ChooseGamedata(inputs);
    CHECK_FALSE(std::filesystem::exists(dir.path / "gamedata.etag"));
    CHECK(feed::FormatProvenance(chosen.provenance) ==
          "Gamedata: bundled gamedata.json; warnings: core config AutoUpdateEnabled and AutoUpdateURL are deprecated and ignored: the "
          "Gamedata Feed replaced the legacy updater");
}

TEST_CASE("Provenance appears in the Resolution Report as structured fields with the startup report line")
{
    const GamedataDir dir;
    const auto feed = FeedOf(3, { "100", "150" }, { { "150", R"({ "disabledEngineFeatures": [{ "id": "cssharp.x/y", "reason": "r" }] })" } });
    Boot(feed, dir, "100");

    CHECK(feed::ProvenanceToJson(Boot(feed, dir, "150").provenance) == nlohmann::json::parse(R"j({
        "source": "ExactBuild", "cached": false, "build": "1.0.150 (SourceRevision 150)", "seq": 3,
        "keyId": "de4a068ed7d94def", "killSwitches": [{ "id": "cssharp.x/y", "reason": "r" }], "warnings": [],
        "line": "Gamedata: exact build 1.0.150 (SourceRevision 150) from the Gamedata Feed, seq 3, key de4a068ed7d94def" })j"));
    CHECK(feed::ProvenanceToJson(Boot(kFeedDown, dir, "200").provenance) == nlohmann::json::parse(R"j({
        "source": "CarriedForward", "cached": true, "build": "1.0.150 (SourceRevision 150)", "seq": 3,
        "keyId": "de4a068ed7d94def", "killSwitches": [{ "id": "cssharp.x/y", "reason": "r" }],
        "warnings": ["Gamedata Feed unreachable: http://feed.test/gamedata/index.json: 404 Not Found",
                     "the Gamedata Feed has no snapshot for build 1.0.200 (SourceRevision 200) on linux yet"],
        "line": "Gamedata: carried forward from build 1.0.150 (SourceRevision 150) in the Gamedata cache, seq 3, key de4a068ed7d94def; warnings: Gamedata Feed unreachable: http://feed.test/gamedata/index.json: 404 Not Found; the Gamedata Feed has no snapshot for build 1.0.200 (SourceRevision 200) on linux yet" })j"));
    CHECK(feed::ProvenanceToJson(feed::ChooseGamedata(Inputs(nullptr)).provenance) == nlohmann::json::parse(R"j({
        "source": "Bundled", "cached": false, "build": null, "seq": null, "keyId": null, "killSwitches": [], "warnings": [],
        "line": "Gamedata: bundled gamedata.json" })j"));
}

// While the server runs: polls share the Gamedata directory with the boot before them.
namespace {

const std::string kChangedSnapshot = R"({ "Build100": { "offsets": { "linux": 2 } }, "Added": { "offsets": { "linux": 5 } },
    "disabledEngineFeatures": [ { "id": "cssharp.chat/say-hooks", "reason": "crashes on this build" } ] })";

feed::PollResult Poll(const http::Transport& transport, const GamedataDir& dir, const std::string& running, const std::string& etag = "")
{
    auto inputs = Inputs(&transport, dir);
    inputs.build = { "1.0.100", "100" };
    return feed::PollGamedataFeed(inputs, running, etag);
}

} // namespace

TEST_CASE("Poll: a newer seq with a changed snapshot for this build is staged and reported once, and the next boot uses it")
{
    const GamedataDir dir;
    const auto running = Boot(FeedOf(1, { "100", "200" }), dir, "100").bytes;

    const auto newer = FeedOf(2, { "100", "200" }, { { "100", kChangedSnapshot } });
    const auto poll = Poll(newer, dir, running);
    REQUIRE(poll.update);
    CHECK(poll.update->build == "1.0.100 (SourceRevision 100)");
    CHECK(poll.update->seq == 2);
    CHECK(poll.update->changes.keys == std::vector<std::string>{ "Added", "Build100" });
    CHECK(poll.update->changes.killSwitches == std::vector<std::string>{ "cssharp.chat/say-hooks" });
    CHECK(poll.warnings.empty());

    CHECK_FALSE(Poll(newer, dir, running).update); // staged already: no second notice

    const auto next = Boot(kFeedDown, dir, "100");
    CHECK(next.bytes == kChangedSnapshot);
    CHECK(next.provenance.seq == 2);
}

TEST_CASE("Poll: the same seq, a newer seq with this build's snapshot unchanged, or a 304 stage nothing")
{
    const GamedataDir dir;
    const auto current = FeedOf(1, { "100" });
    const auto running = Boot(current, dir, "100").bytes;

    SUBCASE("same seq")
    {
        const auto poll = Poll(current, dir, running);
        CHECK_FALSE(poll.update);
        CHECK(poll.warnings.empty());
    }
    SUBCASE("newer seq, only another build changed")
    {
        CHECK_FALSE(Poll(FeedOf(2, { "100", "200" }), dir, running).update);
    }
    SUBCASE("304: not even the signature or a snapshot is fetched")
    {
        const auto newer = FeedOf(2, { "100" }, { { "100", kChangedSnapshot } });
        const auto etag = newer.files.at(kFeedUrl + "/index.json");
        const auto poll = Poll(newer, dir, running, etag);
        CHECK_FALSE(poll.update);
        CHECK(poll.etag == etag);
        CHECK(newer.requested == std::vector<std::string>{ kFeedUrl + "/index.json" });
    }
}

TEST_CASE("Poll: a failed verification or a rollback stages nothing and warns")
{
    const GamedataDir dir;
    const auto running = Boot(FeedOf(3, { "100" }), dir, "100").bytes;

    SUBCASE("snapshot does not match the signed index")
    {
        auto tampered = FeedOf(4, { "100" }, { { "100", kChangedSnapshot } });
        auto& snapshot = tampered.files.at(kFeedUrl + "/snapshots/100/linux.json");
        snapshot = Flip(snapshot, 3);
        const auto poll = Poll(tampered, dir, running);
        CHECK_FALSE(poll.update);
        CHECK(poll.etag.empty()); // retried in full next time
        CHECK(poll.warnings == std::vector<std::string>{ "snapshot snapshots/100/linux.json does not match the signed index (size " +
                                                          std::to_string(kChangedSnapshot.size()) + ", index says " +
                                                          std::to_string(kChangedSnapshot.size()) + ", or sha256 differs)" });
    }
    SUBCASE("index signature invalid")
    {
        auto tampered = FeedOf(4, { "100" }, { { "100", kChangedSnapshot } });
        auto& signature = tampered.files.at(kFeedUrl + "/index.json.sig");
        signature = Flip(signature, 40);
        const auto poll = Poll(tampered, dir, running);
        CHECK_FALSE(poll.update);
        CHECK(poll.warnings == std::vector<std::string>{ "index signature invalid (key de4a068ed7d94def)" });
    }
    SUBCASE("rollback")
    {
        const auto poll = Poll(FeedOf(2, { "100" }, { { "100", kChangedSnapshot } }), dir, running);
        CHECK_FALSE(poll.update);
        CHECK(poll.warnings ==
              std::vector<std::string>{ "Gamedata Feed index seq 2 is below the highest accepted seq 3, rejected as a rollback" });
    }
    SUBCASE("unreachable")
    {
        const auto poll = Poll(kFeedDown, dir, running);
        CHECK_FALSE(poll.update);
        CHECK(poll.warnings == std::vector<std::string>{ "Gamedata Feed unreachable: http://feed.test/gamedata/index.json: 404 Not Found" });
    }
    CHECK(Boot(kFeedDown, dir, "100").bytes == running);
}
