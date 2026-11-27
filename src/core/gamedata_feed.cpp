#include "core/gamedata_feed.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "core/http_client.h"
#include "core/sha256.h"
#include "monocypher-ed25519.h"

namespace counterstrikesharp::feed {
namespace {

namespace fs = std::filesystem;

// The Gamedata cache, in <gamedata dir>/cache: the latest accepted index as kCachedIndex (its signature file followed by
// its bytes, so one atomic write keeps the pair consistent) and one immutable <SourceRevision>.<os>.json per build and OS.
constexpr std::string_view kCacheDir = "cache";
constexpr std::string_view kCachedIndex = "index.signed";
constexpr std::string_view kTempSuffix = ".tmp";
constexpr std::size_t kCachedBuilds = 20;

// shortcut: the only built-in key is the test key (cssharp-gamedata publisher/test-key.pem, private key public), so
// anyone can sign a feed the core trusts; ticket 09 replaces it with the CI signing key and the offline backup key.
constexpr std::string_view kBuiltinKeys[] = {
    "7a8dad7c818602e6381074af43f1bf44903323425627485d51125e2db4f28b5b",
};

constexpr std::size_t kKeyIdSize = 8;
constexpr std::size_t kSignatureSize = 64;

// Download caps (the transport cuts a longer body off): today's Gamedata is ~34 KB.
constexpr std::size_t kIndexCap = 64 * 1024;
constexpr std::size_t kSignatureCap = 256;
constexpr std::size_t kSnapshotCap = 1024 * 1024;

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

std::string Fetch(const http::Transport& transport, const std::string& url, std::size_t maxBytes, std::string& error)
{
    auto result = transport.Get({ url, maxBytes });
    if (!result.body) error = "Gamedata Feed unreachable: " + url + ": " + result.error;
    return result.body.value_or("");
}

// A SourceRevision as a number, or none when it is not one (then it never names a cache file).
std::optional<std::uint64_t> Revision(std::string_view text)
{
    if (text.empty() || text.size() > 19 || !std::all_of(text.begin(), text.end(), [](char c) {
            return c >= '0' && c <= '9';
        }))
        return std::nullopt;
    return std::stoull(std::string(text));
}

// The bundled file's "sourceRevision" stamp; none when it has none, so it counts as the oldest.
std::optional<std::uint64_t> BundledRevision(const std::string& bundled)
{
    const auto root = nlohmann::json::parse(bundled, nullptr, false);
    if (!root.is_object()) return std::nullopt;
    const auto stamp = root.find("sourceRevision");
    if (stamp == root.end() || !stamp->is_string()) return std::nullopt;
    return Revision(stamp->get<std::string>());
}

// An index whose signature verified, parsed.
struct Index
{
    std::string signature;
    std::string bytes;
    nlohmann::json root;
    std::uint64_t seq = 0;
    std::string keyId;
};

std::optional<Index> AcceptIndex(std::string bytes, std::string signature, const std::vector<PublicKey>& keys, std::string& error)
{
    Index index{ std::move(signature), std::move(bytes) };
    if (!VerifyIndex(index.bytes, index.signature, keys, index.keyId, error)) return std::nullopt;
    // Verified: from here on the index is the publisher's.
    try
    {
        index.root = nlohmann::json::parse(index.bytes);
        index.seq = index.root.at("seq").get<std::uint64_t>();
        if (!index.root.at("builds").is_object()) throw std::invalid_argument("\"builds\" is not an object");
    }
    catch (const std::exception& ex)
    {
        error = std::string("index unreadable: ") + ex.what();
        return std::nullopt;
    }
    return index;
}

struct SnapshotEntry
{
    std::string label; // the build's label
    std::string file;
    std::uint64_t size = 0;
    std::string sha256;
};

std::optional<SnapshotEntry> EntryFor(const Index& index, const std::string& revision, const std::string& os)
{
    const auto& builds = index.root.at("builds");
    const auto build = builds.find(revision);
    if (build == builds.end() || !build->is_object() || !build->contains(os)) return std::nullopt;
    const auto& entry = build->at(os);
    return SnapshotEntry{ bindings::BuildIdentity{ build->value("patchVersion", "unknown"), revision }.Label(),
                          entry.at("file").get<std::string>(), entry.at("size").get<std::uint64_t>(),
                          entry.at("sha256").get<std::string>() };
}

bool Matches(std::string_view snapshot, const SnapshotEntry& entry)
{
    const auto digest = Sha256(snapshot);
    return snapshot.size() == entry.size && ToHex(digest.data(), digest.size()) == entry.sha256;
}

// The verified snapshot for entry, or none with the reason in error.
std::optional<std::string> FetchSnapshot(const http::Transport& transport, const std::string& base, const SnapshotEntry& entry,
                                         const std::string& os, std::string& error)
{
    auto snapshot = Fetch(transport, base + "/" + entry.file, kSnapshotCap, error);
    if (!error.empty()) return std::nullopt;
    if (!Matches(snapshot, entry))
    {
        error = "snapshot " + entry.file + " does not match the signed index (size " + std::to_string(snapshot.size()) +
                ", index says " + std::to_string(entry.size) + ", or sha256 differs)";
        return std::nullopt;
    }
    // Hash verified; still Gamedata the resolver can read, or the bundled file is the better choice.
    try
    {
        bindings::ParseGamedataBytes(snapshot, os);
    }
    catch (const std::exception& ex)
    {
        error = "snapshot " + entry.file + " is not readable Gamedata: " + ex.what();
        return std::nullopt;
    }
    return snapshot;
}

fs::path SnapshotPath(const fs::path& cache, const std::string& revision, const std::string& os)
{
    return cache / (revision + "." + os + ".json");
}

// A regular file's bytes; none when it is absent or unreadable.
std::optional<std::string> ReadFile(const fs::path& path)
{
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) return std::nullopt;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

#ifdef _WIN32
int OpenNew(const fs::path& path) { return _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE); }
long long WriteSome(int fd, const char* bytes, std::size_t size)
{
    return _write(fd, bytes, static_cast<unsigned>(std::min<std::size_t>(size, 1u << 30)));
}
int Flush(int fd) { return _commit(fd); }
int Close(int fd) { return _close(fd); }
#else
int OpenNew(const fs::path& path) { return ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644); }
long long WriteSome(int fd, const char* bytes, std::size_t size) { return ::write(fd, bytes, size); }
int Flush(int fd) { return ::fsync(fd); }
int Close(int fd) { return ::close(fd); }
#endif

// Replaces path by bytes atomically: a temp file next to it, flushed to disk, renamed over it (std::filesystem::rename
// replaces an existing file on both OSes). A crash or failure leaves the old file or the new one, never a partial one;
// a temp file left by a crash is removed by the next boot's PruneCache. A failure is a warning (and false): the boot
// goes on.
bool WriteCacheFile(const fs::path& path, std::string_view bytes, std::vector<std::string>& warnings)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const auto temp = fs::path(path) += kTempSuffix;
    const int fd = OpenNew(temp);
    bool ok = fd >= 0;
    for (std::size_t written = 0; ok && written < bytes.size();)
    {
        const auto count = WriteSome(fd, bytes.data() + written, bytes.size() - written);
        if (count > 0) written += static_cast<std::size_t>(count);
        else
            ok = count < 0 && errno == EINTR;
    }
    std::string error = ok ? "" : std::strerror(errno);
    if (ok && Flush(fd) != 0) error = std::strerror(errno);
    if (fd >= 0 && Close(fd) != 0 && error.empty()) error = std::strerror(errno);
    if (error.empty())
    {
        fs::rename(temp, path, ec);
        if (!ec) return true;
        error = ec.message();
    }
    fs::remove(temp, ec);
    warnings.push_back("could not write the Gamedata cache file " + path.string() + ": " + error);
    return false;
}

// Keeps the snapshots of the newest kCachedBuilds builds; removes older ones and temp files left by interrupted writes.
void PruneCache(const fs::path& cache)
{
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& file : fs::directory_iterator(cache, ec))
        files.push_back(file.path());

    std::map<std::uint64_t, std::vector<fs::path>, std::greater<>> builds;
    for (const auto& file : files)
    {
        const auto name = file.filename().string();
        if (name.ends_with(kTempSuffix)) fs::remove(file, ec);
        else if (const auto revision = Revision(name.substr(0, name.find('.'))))
            builds[*revision].push_back(file);
    }
    std::size_t kept = 0;
    for (const auto& [revision, snapshots] : builds)
        if (++kept > kCachedBuilds)
            for (const auto& snapshot : snapshots)
                fs::remove(snapshot, ec);
}

std::string FeedBase(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

// The cached index, re-verified; none when there is none or it does not verify (with a warning).
std::optional<Index> StoredIndex(const fs::path& cache, const std::vector<PublicKey>& keys, std::vector<std::string>& warnings)
{
    const auto file = ReadFile(cache.empty() ? fs::path() : cache / kCachedIndex);
    if (!file) return std::nullopt;
    const auto split = std::min(file->size(), kKeyIdSize + kSignatureSize);
    std::string error;
    auto stored = AcceptIndex(file->substr(split), file->substr(0, split), keys, error);
    if (!stored) warnings.push_back("cached " + error + ", ignored");
    return stored;
}

struct FetchedIndex
{
    std::optional<Index> index; // verified, and not below the floor
    std::string etag; // for the next fetch: the response's, or still the one asked with on a 304; empty when this one failed
};

// The feed's index and signature, verified, with rollback protection: never below floor (the highest seq accepted so far
// or the release's floor). With etag set, asks for the index only if it changed (a 304: no index). A failure is a warning.
FetchedIndex FetchIndex(const http::Transport& transport, const std::string& base, const std::vector<PublicKey>& keys,
                        std::uint64_t floor, const std::string& etag, std::vector<std::string>& warnings)
{
    FetchedIndex fetched;
    auto response = transport.Get({ base + "/index.json", kIndexCap, etag });
    if (response.notModified)
    {
        fetched.etag = etag;
        return fetched;
    }
    std::string error = response.body ? "" : "Gamedata Feed unreachable: " + base + "/index.json: " + response.error;
    auto signature = error.empty() ? Fetch(transport, base + "/index.json.sig", kSignatureCap, error) : "";
    if (error.empty()) fetched.index = AcceptIndex(std::move(*response.body), std::move(signature), keys, error);
    if (fetched.index && fetched.index->seq < floor)
    {
        error = "Gamedata Feed index seq " + std::to_string(fetched.index->seq) + " is below the highest accepted seq " +
                std::to_string(floor) + ", rejected as a rollback";
        fetched.index.reset();
    }
    if (error.empty()) fetched.etag = response.etag;
    else
        warnings.push_back(error);
    return fetched;
}

// Makes the fetched index the cached one (the seq state) unless it already is.
void CacheIndex(const fs::path& cache, const std::optional<Index>& stored, const Index& fetched, std::vector<std::string>& warnings)
{
    if (!cache.empty() && (!stored || stored->signature != fetched.signature || stored->bytes != fetched.bytes))
        WriteCacheFile(cache / kCachedIndex, fetched.signature + fetched.bytes, warnings);
}

// The ids or keys of two id-to-value maps whose value differs or that only one of them has, sorted.
template <typename Map> std::vector<std::string> Differing(const Map& before, const Map& after)
{
    std::set<std::string> differing;
    for (const auto& [key, value] : before)
        if (const auto other = after.find(key); other == after.end() || !(other->second == value)) differing.insert(key);
    for (const auto& [key, value] : after)
        if (!before.contains(key)) differing.insert(key);
    return { differing.begin(), differing.end() };
}

GamedataChanges DiffGamedata(std::string_view running, std::string_view staged, const std::string& os)
{
    const auto before = nlohmann::json::parse(running);
    const auto after = nlohmann::json::parse(staged);
    const auto kills = [](const nlohmann::json& root) {
        std::map<std::string, std::string> reasons;
        for (const auto& kill : bindings::ParseKillSwitches(root))
            reasons[kill.id] = kill.reason;
        return reasons;
    };
    return { Differing(bindings::ParseGamedata(before, os), bindings::ParseGamedata(after, os)), Differing(kills(before), kills(after)) };
}

ChosenGamedata FromIndex(std::string bytes, GamedataSource source, bool cached, const Index& index, std::string build)
{
    ChosenGamedata chosen{ std::move(bytes), {} };
    chosen.provenance.source = source;
    chosen.provenance.cached = cached;
    chosen.provenance.build = std::move(build);
    chosen.provenance.seq = index.seq;
    chosen.provenance.keyId = index.keyId;
    return chosen;
}

// The selection of ChooseGamedata with the feed on; the reasons for skipping a step are appended to warnings.
ChosenGamedata Choose(const GamedataSourceInputs& inputs, std::vector<std::string>& warnings)
{
    const ChosenGamedata bundled{ inputs.bundled, {} };
    const auto& revision = inputs.build.sourceRevision;
    if (!Revision(revision))
    {
        warnings.push_back("this build's SourceRevision is unknown (no steam.inf), no snapshot to pick");
        return bundled;
    }

    const auto base = FeedBase(inputs.feedUrl);
    const auto keys = TrustedKeys(inputs.publicKeys, warnings);
    const auto cache = inputs.gamedataDir.empty() ? fs::path() : inputs.gamedataDir / kCacheDir;

    const auto stored = StoredIndex(cache, keys, warnings);
    const auto fetched =
        FetchIndex(*inputs.transport, base, keys, std::max(inputs.seqFloor, stored ? stored->seq : 0), "", warnings).index;
    if (fetched) CacheIndex(cache, stored, *fetched, warnings);

    const Index* index = fetched ? &*fetched : stored ? &*stored : nullptr;
    if (index == nullptr) return bundled;

    // 1. Exactly this build: cached, or fetched when the feed answered.
    if (const auto entry = EntryFor(*index, revision, inputs.os))
    {
        const auto path = cache.empty() ? fs::path() : SnapshotPath(cache, revision, inputs.os);
        if (auto cached = ReadFile(path))
        {
            if (Matches(*cached, *entry)) return FromIndex(std::move(*cached), GamedataSource::ExactBuild, true, *index, entry->label);
            warnings.push_back("cached snapshot for build " + entry->label + " does not match the signed index, ignored");
        }
        if (fetched)
        {
            std::string error;
            if (auto snapshot = FetchSnapshot(*inputs.transport, base, *entry, inputs.os, error))
            {
                if (!path.empty()) WriteCacheFile(path, *snapshot, warnings);
                return FromIndex(std::move(*snapshot), GamedataSource::ExactBuild, false, *index, entry->label);
            }
            warnings.push_back(error);
        }
    }
    else
        warnings.push_back("the Gamedata Feed has no snapshot for build " + inputs.build.Label() + " on " + inputs.os + " yet");

    // 2. Carry forward the newest of the verified cached snapshots and the bundled file.
    // shortcut: a cached snapshot is verified only against the one accepted index, so a build the publisher drops from
    // the index can no longer be carried forward; keep per-snapshot proof (index + signature) if the feed ever prunes.
    ChosenGamedata newest = bundled;
    auto newestRevision = BundledRevision(inputs.bundled);
    if (cache.empty()) return newest;
    for (const auto& [other, build] : index->root.at("builds").items())
    {
        const auto otherRevision = Revision(other);
        const auto entry = otherRevision ? EntryFor(*index, other, inputs.os) : std::nullopt;
        if (!entry || other == revision || (newestRevision && *otherRevision <= *newestRevision)) continue;
        auto cached = ReadFile(SnapshotPath(cache, other, inputs.os));
        if (!cached) continue;
        if (!Matches(*cached, *entry))
        {
            warnings.push_back("cached snapshot for build " + entry->label + " does not match the signed index, skipped");
            continue;
        }
        newest = FromIndex(std::move(*cached), GamedataSource::CarriedForward, true, *index, entry->label);
        newestRevision = otherRevision;
    }
    return newest;
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
    std::vector<std::string> warnings;
    if (!inputs.legacyConfigKeys.empty())
    {
        std::string keys;
        for (const auto& key : inputs.legacyConfigKeys)
            keys += (keys.empty() ? "" : " and ") + key;
        warnings.push_back("core config " + keys + (inputs.legacyConfigKeys.size() == 1 ? " is" : " are") +
                           " deprecated and ignored: the Gamedata Feed replaced the legacy updater");
    }
    if (!inputs.gamedataDir.empty())
    {
        std::error_code ec;
        fs::remove(inputs.gamedataDir / "gamedata.etag", ec); // the legacy updater's
        PruneCache(inputs.gamedataDir / kCacheDir);
    }

    ChosenGamedata chosen{ inputs.bundled, {} };
    if (inputs.transport != nullptr)
    {
        try
        {
            chosen = Choose(inputs, warnings);
        }
        catch (const std::exception& ex)
        {
            chosen = { inputs.bundled, {} };
            warnings.push_back(std::string("Gamedata Feed index unreadable: ") + ex.what());
        }
    }
    chosen.provenance.warnings = std::move(warnings);
    return chosen;
}

PollResult PollGamedataFeed(const GamedataSourceInputs& inputs, std::string_view running, const std::string& etag)
{
    PollResult result;
    const auto& revision = inputs.build.sourceRevision;
    if (inputs.transport == nullptr || inputs.gamedataDir.empty() || !Revision(revision)) return result;
    try
    {
        const auto base = FeedBase(inputs.feedUrl);
        const auto keys = TrustedKeys(inputs.publicKeys, result.warnings);
        const auto cache = inputs.gamedataDir / kCacheDir;
        const auto stored = StoredIndex(cache, keys, result.warnings);
        const auto fetched =
            FetchIndex(*inputs.transport, base, keys, std::max(inputs.seqFloor, stored ? stored->seq : 0), etag, result.warnings);
        result.etag = fetched.etag;
        if (!fetched.index) return result;

        const auto& index = *fetched.index;
        if (const auto entry = EntryFor(index, revision, inputs.os))
        {
            const auto path = SnapshotPath(cache, revision, inputs.os);
            const auto staged = ReadFile(path);
            if (!Matches(running, *entry) && !(staged && Matches(*staged, *entry)))
            {
                result.etag.clear(); // unless staged below, fetch it all again next time
                std::string error;
                const auto snapshot = FetchSnapshot(*inputs.transport, base, *entry, inputs.os, error);
                if (!snapshot)
                {
                    result.warnings.push_back(error);
                    return result;
                }
                if (!WriteCacheFile(path, *snapshot, result.warnings)) return result;
                result.update = GamedataUpdate{ entry->label, index.seq, DiffGamedata(running, *snapshot, inputs.os) };
                result.etag = fetched.etag;
            }
        }
        // After the snapshot: a crash in between leaves the old index, and the next boot fetches the snapshot again.
        CacheIndex(cache, stored, index, result.warnings);
    }
    catch (const std::exception& ex)
    {
        result.update.reset();
        result.etag.clear();
        result.warnings.push_back(std::string("Gamedata Feed index unreadable: ") + ex.what());
    }
    return result;
}

std::string FormatProvenance(const Provenance& provenance)
{
    const auto vouched = ", seq " + std::to_string(provenance.seq.value_or(0)) + ", key " + provenance.keyId;
    std::string text = provenance.source == GamedataSource::ExactBuild
                           ? "Gamedata: exact build " + provenance.build + " from the Gamedata " +
                                 (provenance.cached ? "cache" : "Feed") + vouched
                       : provenance.source == GamedataSource::CarriedForward
                           ? "Gamedata: carried forward from build " + provenance.build + " in the Gamedata cache" + vouched
                           : "Gamedata: bundled gamedata.json";
    for (std::size_t i = 0; i < provenance.warnings.size(); ++i)
        text += (i == 0 ? "; warnings: " : "; ") + provenance.warnings[i];
    return text;
}

nlohmann::json ProvenanceToJson(const Provenance& provenance)
{
    const auto labelIf = [&](GamedataSource source) {
        return provenance.source == source ? nlohmann::json(provenance.build) : nlohmann::json();
    };
    const bool vouched = provenance.source != GamedataSource::Bundled;
    return {
        { "source", provenance.source == GamedataSource::ExactBuild       ? "ExactBuild"
                    : provenance.source == GamedataSource::CarriedForward ? "CarriedForward"
                                                                          : "Bundled" },
        { "cached", provenance.cached },
        { "build", labelIf(GamedataSource::ExactBuild) },
        { "carriedFrom", labelIf(GamedataSource::CarriedForward) },
        { "seq", vouched && provenance.seq ? nlohmann::json(*provenance.seq) : nlohmann::json() },
        { "keyId", vouched ? nlohmann::json(provenance.keyId) : nlohmann::json() },
        { "warnings", provenance.warnings },
        { "line", FormatProvenance(provenance) },
    };
}

} // namespace counterstrikesharp::feed
