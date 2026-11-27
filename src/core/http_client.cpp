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

} // namespace counterstrikesharp::http
