#pragma once

#include <memory>
#include <string>

namespace httplib {
class Client;
}

namespace counterstrikesharp::http {

// A cpp-httplib client for url ("http://host[:port]"), or null with the reason in error. Never throws: an https:// URL
// (the core links no TLS stack) or a malformed URL is an error, not an abort (upstream #931). Requests on the client
// report failure through their result.
std::unique_ptr<httplib::Client> MakeClient(const std::string& url, std::string& error);

} // namespace counterstrikesharp::http
