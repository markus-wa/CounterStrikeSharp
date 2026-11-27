#pragma once

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

struct FetchResult
{
    std::optional<std::string> body; // a 200 response's body
    std::string error; // why there is no body
};

// "Fetch this URL": the Gamedata Source's only way to the network, so tests use fakes.
class Transport
{
  public:
    virtual ~Transport() = default;
    // GET url ("http://host[:port]/path"). Never throws.
    virtual FetchResult Get(const std::string& url) const = 0;
};

// The real transport, on cpp-httplib through MakeClient. Redirects are not followed.
// shortcut: only connect and read timeouts, no body caps or wall-clock deadline (DNS is unbounded); ticket 04 hardens it.
class HttpTransport final : public Transport
{
  public:
    FetchResult Get(const std::string& url) const override;
};

} // namespace counterstrikesharp::http
