#include "core/http_client.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include "httplib/httplib.h"

namespace counterstrikesharp::http {
namespace {

// A cpp-httplib client for url ("http://host[:port]"), or null with the reason in error. Never throws.
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

// shortcut: cpp-httplib has no limit on the headers' total, only per line (CPPHTTPLIB_HEADER_MAX_LENGTH, 8 KiB) and
// count (CPPHTTPLIB_HEADER_MAX_COUNT, 100), and reads them whole before Refusal sees them: up to ~800 KiB is buffered
// (within the deadline) before this cap refuses the response. Read the headers ourselves if that memory ever matters.
constexpr std::size_t kHeaderCap = 8 * 1024;
constexpr std::chrono::milliseconds kConnectTimeout{ 3000 };

// Why a response is not read past its headers, or empty to read its body. A 304 is not refused, but has no body to read.
std::string Refusal(const httplib::Response& response)
{
    std::size_t headerBytes = 0;
    for (const auto& [name, value] : response.headers)
        headerBytes += name.size() + value.size() + 4; // "name: value\r\n"
    if (headerBytes > kHeaderCap) return "response headers exceed the " + std::to_string(kHeaderCap) + "-byte cap";
    if (response.status == 304) return "";
    if (response.status / 100 == 3)
        return "redirect refused (HTTP " + std::to_string(response.status) + " to " + response.get_header_value("Location") + ")";
    if (response.status != 200) return "HTTP " + std::to_string(response.status);
    const auto encoding = response.get_header_value("Content-Encoding");
    if (!encoding.empty() && encoding != "identity") return "compressed response refused (Content-Encoding: " + encoding + ")";
    return "";
}

// One GET under request.maxBytes (capError past it). cpp-httplib's own timeouts end it after timeout once connected, so
// an abandoned worker finishes too; it stops early on the next chunk once abandoned is set.
FetchResult
Fetch(const Request& request, const std::string& capError, std::chrono::milliseconds timeout, const std::atomic<bool>& abandoned)
{
    // MakeClient takes "scheme://host[:port]" only; the rest is the request path.
    const auto& url = request.url;
    const auto scheme = url.find("://");
    const auto slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    FetchResult out;
    const auto client = MakeClient(url.substr(0, slash), out.error);
    if (!client) return out;
    try
    {
        client->set_connection_timeout(kConnectTimeout);
        client->set_read_timeout(timeout);
        client->set_write_timeout(timeout);
        client->set_max_timeout(timeout);
        httplib::Headers headers{ { "Accept-Encoding", "identity" } };
        if (!request.ifNoneMatch.empty()) headers.emplace("If-None-Match", request.ifNoneMatch);
        std::string body;
        const auto overCap = [&](std::uint64_t length) {
            if (length <= request.maxBytes) return false;
            out.error = capError;
            return true;
        };
        const auto result =
            client->Get(slash == std::string::npos ? "/" : url.substr(slash), headers, [&](const httplib::Response& response) {
            out.error = Refusal(response);
            out.notModified = out.error.empty() && response.status == 304;
            out.etag = response.get_header_value("ETag");
            return out.error.empty() && !out.notModified && !overCap(response.get_header_value_u64("Content-Length"));
        }, [&](const char* data, std::size_t length) {
            if (abandoned || overCap(body.size() + length)) return false;
            body.append(data, length);
            return true;
        });
        if (!out.error.empty() || out.notModified) return out;
        if (!result) return { std::nullopt, httplib::to_string(result.error()) };
        out.body = std::move(body);
        return out;
    }
    catch (const std::exception& ex)
    {
        return { std::nullopt, ex.what() };
    }
}

} // namespace

HttpTransport::HttpTransport(Limits limits)
    : m_limits(limits), m_deadline(std::chrono::steady_clock::now() + limits.deadline), m_bytesLeft(limits.totalBytes)
{
}

FetchResult HttpTransport::Get(const Request& request) const
{
    const std::string deadlineError = "deadline of " + std::to_string(m_limits.deadline.count()) + " ms exceeded";
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(m_deadline - std::chrono::steady_clock::now());
    if (left <= std::chrono::milliseconds::zero()) return { std::nullopt, deadlineError };
    const auto timeout = left + std::chrono::seconds(1); // the worker's own timeouts: past the deadline, which the wait below enforces

    auto capped = request;
    std::string capError = "response exceeds the " + std::to_string(request.maxBytes) + "-byte cap";
    if (m_bytesLeft < request.maxBytes)
    {
        capped.maxBytes = m_bytesLeft;
        capError = "responses exceed the " + std::to_string(m_limits.totalBytes) + "-byte total of this fetch";
    }

    // Shared with the worker, which may outlive this call.
    struct Pending
    {
        std::mutex mutex;
        std::condition_variable done;
        std::optional<FetchResult> result;
        std::atomic<bool> abandoned{ false };
    };
    const auto pending = std::make_shared<Pending>();
    try
    {
        std::thread([pending, capped, capError, timeout] {
            auto result = Fetch(capped, capError, timeout, pending->abandoned);
            const std::lock_guard lock(pending->mutex);
            pending->result = std::move(result);
            pending->done.notify_one();
        }).detach();
    }
    catch (const std::exception& ex)
    {
        return { std::nullopt, ex.what() };
    }

    std::unique_lock lock(pending->mutex);
    if (!pending->done.wait_until(lock, m_deadline, [&] {
        return pending->result.has_value();
    }))
    {
        pending->abandoned = true;
        return { std::nullopt, deadlineError };
    }
    if (pending->result->body) m_bytesLeft -= pending->result->body->size();
    return std::move(*pending->result);
}

} // namespace counterstrikesharp::http
