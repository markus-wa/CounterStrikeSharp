#include <doctest.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "core/http_client.h"
#include "httplib/httplib.h"

using counterstrikesharp::http::HttpTransport;
using namespace std::chrono_literals;

namespace {

constexpr std::size_t kCap = 1024;

// A local HTTP server on a free port, serving the routes setup adds, until it goes out of scope.
struct TestServer
{
    httplib::Server server;
    std::thread thread;
    std::string origin;

    explicit TestServer(const std::function<void(httplib::Server&)>& setup)
    {
        setup(server);
        const int port = server.bind_to_any_port("127.0.0.1");
        origin = "http://127.0.0.1:" + std::to_string(port);
        thread = std::thread([this] {
            server.listen_after_bind();
        });
        server.wait_until_ready();
    }
    ~TestServer()
    {
        server.stop();
        thread.join();
    }
};

} // namespace

TEST_CASE("The real transport fetches a path under the feed URL and reports anything but a 200 as an error")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/feed/index.json", [](const httplib::Request&, httplib::Response& response) {
            response.set_content("{\"seq\": 1}", "application/json");
        });
    });
    const HttpTransport transport;

    const auto fetched = transport.Get({ feed.origin + "/feed/index.json", kCap });
    CHECK(fetched.body == "{\"seq\": 1}");
    const auto missing = transport.Get({ feed.origin + "/feed/missing.json", kCap });
    CHECK_FALSE(missing.body);
    CHECK(missing.error == "HTTP 404");
}

// Upstream #931: an https:// AutoUpdateURL aborted the server, since the core links no TLS stack.
TEST_CASE("The real transport reports an https, malformed or unreachable URL as an error instead of throwing")
{
    const HttpTransport transport;
    const auto https = transport.Get({ "https://gamedata.cssharp.dev/index.json", kCap });
    CHECK(https.error.find("https") != std::string::npos);
    for (const char* url : { "https://gamedata.cssharp.dev/index.json", "ftp://gamedata.cssharp.dev:99999999999/index.json", "not a url",
                             "http://127.0.0.1:1/index.json" })
    {
        CAPTURE(url);
        const auto result = transport.Get({ url, kCap });
        CHECK_FALSE(result.body);
        CHECK_FALSE(result.error.empty());
    }
}

TEST_CASE("The real transport cuts a body off at the request's cap instead of buffering it")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/exact", [](const httplib::Request&, httplib::Response& response) {
            response.set_content(std::string(kCap, 'x'), "application/json");
        });
        server.Get("/oversized", [](const httplib::Request&, httplib::Response& response) {
            response.set_content(std::string(kCap + 1, 'x'), "application/json");
        });
        server.Get("/oversized-chunked", [](const httplib::Request&, httplib::Response& response) {
            // No Content-Length up front: the cap must hold while the body streams in.
            response.set_chunked_content_provider("application/json", [](std::size_t, httplib::DataSink& sink) {
                const std::string chunk(512, 'x');
                for (int i = 0; i < 8 && sink.is_writable(); ++i)
                    sink.write(chunk.data(), chunk.size());
                sink.done();
                return true;
            });
        });
    });
    const HttpTransport transport;

    CHECK(transport.Get({ feed.origin + "/exact", kCap }).body == std::string(kCap, 'x'));
    for (const char* path : { "/oversized", "/oversized-chunked" })
    {
        CAPTURE(path);
        const auto result = transport.Get({ feed.origin + path, kCap });
        CHECK_FALSE(result.body);
        CHECK(result.error == "response exceeds the 1024-byte cap");
    }
}

TEST_CASE("The real transport refuses a redirect instead of following it")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/index.json", [](const httplib::Request&, httplib::Response& response) {
            response.set_content("{}", "application/json");
        });
        server.Get("/moved", [](const httplib::Request&, httplib::Response& response) {
            response.set_redirect("/index.json");
        });
    });
    const auto result = HttpTransport().Get({ feed.origin + "/moved", kCap });
    CHECK_FALSE(result.body);
    CHECK(result.error == "redirect refused (HTTP 302 to /index.json)");
}

TEST_CASE("The real transport asks for an uncompressed body and refuses a compressed one")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/index.json", [](const httplib::Request& request, httplib::Response& response) {
            if (request.get_header_value("Accept-Encoding") != "identity") response.set_header("Content-Encoding", "gzip");
            response.set_content("{}", "application/json");
        });
        server.Get("/gzipped", [](const httplib::Request&, httplib::Response& response) {
            response.set_header("Content-Encoding", "gzip");
            response.set_content("\x1f\x8b not really gzip", "application/json");
        });
    });
    const HttpTransport transport;
    CHECK(transport.Get({ feed.origin + "/index.json", kCap }).body == "{}");
    const auto gzipped = transport.Get({ feed.origin + "/gzipped", kCap });
    CHECK_FALSE(gzipped.body);
    CHECK(gzipped.error == "compressed response refused (Content-Encoding: gzip)");
}

// Ticket 13's background poll asks with the index's last ETag, so an unchanged feed costs one empty response.
TEST_CASE("The real transport sends If-None-Match and reports a 304 as unchanged")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/index.json", [](const httplib::Request& request, httplib::Response& response) {
            response.set_header("ETag", "\"v1\"");
            if (request.get_header_value("If-None-Match") == "\"v1\"") response.status = 304;
            else
                response.set_content("{\"seq\": 1}", "application/json");
        });
    });
    const HttpTransport transport;

    const auto first = transport.Get({ feed.origin + "/index.json", kCap });
    CHECK(first.body == "{\"seq\": 1}");
    CHECK(first.etag == "\"v1\"");
    CHECK_FALSE(first.notModified);

    const auto unchanged = transport.Get({ feed.origin + "/index.json", kCap, first.etag });
    CHECK(unchanged.notModified);
    CHECK_FALSE(unchanged.body);
    CHECK(unchanged.error.empty());

    const auto stale = transport.Get({ feed.origin + "/index.json", kCap, "\"v0\"" });
    CHECK(stale.body == "{\"seq\": 1}");
    CHECK_FALSE(stale.notModified);
}

TEST_CASE("The real transport stops a slow-drip response at its wall-clock deadline, which holds for every later fetch too")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/drip", [](const httplib::Request&, httplib::Response& response) {
            // One byte every 50 ms: never idle long enough for a read timeout, never done.
            response.set_chunked_content_provider("application/json", [](std::size_t, httplib::DataSink& sink) {
                std::this_thread::sleep_for(50ms);
                return sink.write("x", 1);
            });
        });
        server.Get("/index.json", [](const httplib::Request&, httplib::Response& response) {
            response.set_content("{}", "application/json");
        });
    });
    const HttpTransport transport({ .deadline = 500ms });

    const auto start = std::chrono::steady_clock::now();
    const auto dripped = transport.Get({ feed.origin + "/drip", kCap });
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK_FALSE(dripped.body);
    CHECK(dripped.error == "deadline of 500 ms exceeded");
    CHECK(elapsed >= 450ms);
    CHECK(elapsed < 1s);

    const auto later = transport.Get({ feed.origin + "/index.json", kCap }); // the same boot fetch: its deadline has passed
    CHECK_FALSE(later.body);
    CHECK(later.error == "deadline of 500 ms exceeded");
    CHECK(HttpTransport({ .deadline = 500ms }).Get({ feed.origin + "/index.json", kCap }).body == "{}"); // a new fetch, a new deadline
}

TEST_CASE("The real transport gives up on an unresolvable host within its deadline")
{
    const auto start = std::chrono::steady_clock::now();
    const auto result = HttpTransport({ .deadline = 2s }).Get({ "http://gamedata-feed.invalid/index.json", kCap });
    CHECK_FALSE(result.body);
    CHECK_FALSE(result.error.empty());
    CHECK(std::chrono::steady_clock::now() - start < 2500ms);
}

TEST_CASE("The real transport caps all bodies of one fetch together")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/snapshot.json", [](const httplib::Request&, httplib::Response& response) {
            response.set_content(std::string(kCap, 'x'), "application/json");
        });
    });
    const HttpTransport transport({ .totalBytes = kCap + kCap / 2 });
    CHECK(transport.Get({ feed.origin + "/snapshot.json", kCap }).body);
    const auto second = transport.Get({ feed.origin + "/snapshot.json", kCap });
    CHECK_FALSE(second.body);
    CHECK(second.error == "responses exceed the 1536-byte total of this fetch");
}

// The spec's limits; Windows runs the same transport (no per-OS limits, no Windows native tests in CI).
TEST_CASE("The default limits are one 10 s deadline and 2 MiB in total per fetch")
{
    const counterstrikesharp::http::Limits limits;
    CHECK(limits.deadline == 10s);
    CHECK(limits.totalBytes == 2 * 1024 * 1024);
}

TEST_CASE("The real transport refuses response headers over 8 KiB")
{
    const TestServer feed([](httplib::Server& server) {
        server.Get("/index.json", [](const httplib::Request&, httplib::Response& response) {
            response.set_header("X-Padding-1", std::string(4096, 'x'));
            response.set_header("X-Padding-2", std::string(4096, 'x'));
            response.set_content("{}", "application/json");
        });
    });
    const auto result = HttpTransport().Get({ feed.origin + "/index.json", kCap });
    CHECK_FALSE(result.body);
    CHECK(result.error == "response headers exceed the 8192-byte cap");
}
