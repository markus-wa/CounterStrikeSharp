#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace httplib {
class Client;
}

namespace counterstrikesharp::http {

// A cpp-httplib client for url ("http://host[:port]"), or null with the reason in error. Never throws: an https:// URL
// (the core links no TLS stack) or a malformed URL is an error, not an abort (upstream #931). Requests on the client
// report failure through their result.
std::unique_ptr<httplib::Client> MakeClient(const std::string& url, std::string& error);

struct Request
{
    std::string url; // "http://host[:port]/path"
    std::size_t maxBytes = 0; // body cap: a longer body is an error, never a truncated body
    std::string ifNoneMatch; // an earlier FetchResult's etag: the server may answer "unchanged" (notModified)
};

struct FetchResult
{
    std::optional<std::string> body; // a 200 response's body
    std::string error; // why there is neither a body nor notModified
    std::string etag; // the response's ETag, for a later Request's ifNoneMatch
    bool notModified = false; // a 304: still what ifNoneMatch named, no body
};

// "Fetch this URL": the Gamedata Source's only way to the network, so tests use fakes.
class Transport
{
  public:
    virtual ~Transport() = default;
    // GET request.url. Never throws: every refusal or limit hit is FetchResult::error, for the caller's provenance.
    virtual FetchResult Get(const Request& request) const = 0;
};

// What one fetch (a boot's Gamedata fetch, or one poll) may spend, on every OS.
struct Limits
{
    std::chrono::milliseconds deadline{ 10000 }; // wall clock from the transport's construction, DNS included
    std::size_t totalBytes = 2 * 1024 * 1024; // all bodies together
};

// The real transport, on cpp-httplib through MakeClient. One instance is one fetch: its Limits start at construction
// and span all its Gets, so make a new one per boot fetch or poll. Beyond them: 3 s to connect, response headers
// capped at 8 KiB, Accept-Encoding identity (a compressed body is refused), redirects refused. Not for concurrent Gets.
// Each Get runs on its own thread so the deadline also bounds blocking DNS (getaddrinfo has no timeout and the
// non-blocking variant needs libanl); on the deadline the caller returns and the thread is left to finish alone.
// shortcut: a detached thread stuck in getaddrinfo outlives the deadline (until the resolver gives up); if the plugin
// unloads in that window it runs unmapped code. Upgrade if unload right after a feed timeout is ever seen.
class HttpTransport final : public Transport
{
  public:
    explicit HttpTransport(Limits limits = {});
    FetchResult Get(const Request& request) const override;

  private:
    Limits limits_;
    std::chrono::steady_clock::time_point deadline_;
    mutable std::size_t bytesLeft_;
};

} // namespace counterstrikesharp::http
