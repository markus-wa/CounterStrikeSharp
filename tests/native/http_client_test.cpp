#include <doctest.h>

#include <string>

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

TEST_CASE("A plain http URL gives a client whose failed request is a result, not an exception")
{
    std::string error;
    const auto client = MakeClient("http://127.0.0.1:1", error);
    REQUIRE(client != nullptr);
    CHECK(error.empty());
    CHECK_FALSE(client->Head("/"));
}
