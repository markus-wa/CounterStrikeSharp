#include <doctest.h>

#include <string>
#include <thread>

#include "core/http_client.h"
#include "httplib/httplib.h"

using counterstrikesharp::http::MakeClient;

// Upstream #931: an https:// AutoUpdateURL aborted the server, since the core links no TLS stack.
TEST_CASE("An https URL gives no client and a reason instead of aborting")
{
    std::string error;
    CHECK(MakeClient("https://gamedata.cssharp.dev", error) == nullptr);
    CHECK(error.find("https") != std::string::npos);
}

TEST_CASE("A malformed URL gives no client and a reason instead of throwing")
{
    for (const char* url : { "ftp://gamedata.cssharp.dev", "http://gamedata.cssharp.dev:99999999999" })
    {
        CAPTURE(url);
        std::string error;
        CHECK(MakeClient(url, error) == nullptr);
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("The real transport fetches a path under the feed URL and reports anything but a 200 as an error")
{
    httplib::Server server;
    server.Get("/feed/index.json", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("{\"seq\": 1}", "application/json");
    });
    server.Get("/moved", [](const httplib::Request&, httplib::Response& response) {
        response.set_redirect("/feed/index.json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    std::thread thread([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    const std::string origin = "http://127.0.0.1:" + std::to_string(port);
    const counterstrikesharp::http::HttpTransport transport;

    const auto fetched = transport.Get(origin + "/feed/index.json");
    CHECK(fetched.body == "{\"seq\": 1}");
    const auto missing = transport.Get(origin + "/feed/missing.json");
    CHECK_FALSE(missing.body);
    CHECK(missing.error == "HTTP 404");
    const auto redirected = transport.Get(origin + "/moved"); // redirects are not followed
    CHECK_FALSE(redirected.body);
    CHECK(redirected.error == "HTTP 302");

    server.stop();
    thread.join();
}

TEST_CASE("The real transport reports an https, malformed or unreachable URL as an error instead of throwing")
{
    const counterstrikesharp::http::HttpTransport transport;
    for (const char* url : { "https://gamedata.cssharp.dev/index.json", "ftp://gamedata.cssharp.dev:99999999999/index.json",
                             "not a url", "http://127.0.0.1:1/index.json" })
    {
        CAPTURE(url);
        const auto result = transport.Get(url);
        CHECK_FALSE(result.body);
        CHECK_FALSE(result.error.empty());
    }
}

TEST_CASE("A plain http URL gives a client whose failed request is a result, not an exception")
{
    std::string error;
    const auto client = MakeClient("http://127.0.0.1:1", error);
    REQUIRE(client != nullptr);
    CHECK(error.empty());
    CHECK_FALSE(client->Head("/"));
}
