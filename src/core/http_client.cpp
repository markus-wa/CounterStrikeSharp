#include "core/http_client.h"

#include <exception>

#include "httplib/httplib.h"

namespace counterstrikesharp::http {

std::unique_ptr<httplib::Client> MakeClient(const std::string& url, std::string& error)
{
    try
    {
        auto client = std::make_unique<httplib::Client>(url);
        // Built without OpenSSL, cpp-httplib leaves an https:// client without an implementation instead of throwing.
        if (client->is_valid()) return client;
        error = "unsupported URL '" + url + "' (https is not supported, use http)";
    }
    catch (const std::exception& ex)
    {
        error = "invalid URL '" + url + "': " + ex.what();
    }
    return nullptr;
}

FetchResult HttpTransport::Get(const std::string& url) const
{
    // MakeClient takes "scheme://host[:port]" only; the rest is the request path.
    const auto scheme = url.find("://");
    const auto slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    std::string error;
    const auto client = MakeClient(url.substr(0, slash), error);
    if (!client) return { std::nullopt, error };
    try
    {
        client->set_connection_timeout(3, 0);
        client->set_read_timeout(10, 0);
        const auto result = client->Get(slash == std::string::npos ? "/" : url.substr(slash));
        if (!result) return { std::nullopt, httplib::to_string(result.error()) };
        if (result->status != 200) return { std::nullopt, "HTTP " + std::to_string(result->status) };
        return { result->body, "" };
    }
    catch (const std::exception& ex)
    {
        return { std::nullopt, ex.what() };
    }
}

} // namespace counterstrikesharp::http
