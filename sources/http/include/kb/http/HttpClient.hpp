#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// A small HTTP and HTTPS client for gameplay: web APIs, leaderboards, account services. Requests run on
// worker threads and finish by a callback that Poll() runs on the thread that calls it, so game code
// needs no locking. Every request has a deadline, a limit on the size of the answer, a limit on
// redirects, and can be cancelled. HTTPS is checked by the operating system (the system's trusted
// certificates and the name of the host); a service with its own certificate can be pinned instead.
//
// Platforms: Windows uses WinHTTP. Elsewhere every request ends with Status::Unsupported and an
// explanation; there is no bundled TLS library, and none is added silently.
//
// What it is not: not a browser (no cookies, no cache, no HTTP/2 push, no automatic decompression), and
// not a download manager (the whole answer is held in memory, up to the limit).
namespace kb::http {

struct Header {
    std::string name;
    std::string value;
};

struct Request {
    std::string method = "GET"; // GET, HEAD, POST, PUT, PATCH or DELETE
    std::string url;            // http:// or https://, no user name or password in it
    std::vector<Header> headers;
    std::vector<std::uint8_t> body; // only with POST, PUT and PATCH
    std::uint32_t timeoutMilliseconds = 10'000U;   // for the whole request, redirects included
    std::size_t maxResponseBytes = 1U << 20U;      // an answer over this is cut off and reported as TooLarge
    std::uint32_t maxRedirects = 5U;
    bool allowInsecureHttp = false;                // plain http:// is refused unless this is set
    // 64 hex digits (SHA-256 of the certificate): the server's certificate must be exactly this one, and then
    // its chain and its name are not checked. Empty: the system decides.
    std::string pinnedCertificateSha256;
};

enum class Status : std::uint8_t {
    Ok,              // an answer arrived; the HTTP status says what it means
    InvalidRequest,  // the request itself is not acceptable (see Response::error)
    InsecureBlocked, // http:// without allowInsecureHttp, or a redirect from https to http
    ConnectionFailed,
    Timeout,
    CertificateError,
    TooLarge,
    TooManyRedirects,
    RedirectRefused, // a redirect that is not safe to follow
    Cancelled,
    Unsupported,     // this platform has no backend
    Error,
};

struct Response {
    Status status = Status::Error;
    int httpStatus = 0; // 200, 404, ...; 0 when no answer arrived
    std::vector<Header> headers;
    std::vector<std::uint8_t> body;
    std::string error;    // empty when status is Ok
    std::string finalUrl; // after redirects
    std::uint32_t redirects = 0U;
    std::uint64_t elapsedMilliseconds = 0U;
};

// Checks everything about a request that can be checked without the network. False with `error` set.
[[nodiscard]] bool ValidateRequest(const Request& request, std::string& error);

class CancelToken;

// A request in flight. Cancel is safe from any thread and at any time; a cancelled request finishes with
// Status::Cancelled (or with its real result if it had already finished).
class RequestHandle {
public:
    RequestHandle() = default;
    void Cancel() const;
    [[nodiscard]] bool Valid() const noexcept { return state_ != nullptr; }
    [[nodiscard]] bool Finished() const;

private:
    friend class Client;
    explicit RequestHandle(std::shared_ptr<CancelToken> state) : state_(std::move(state)) {}
    std::shared_ptr<CancelToken> state_;
};

struct ClientLimits {
    std::size_t workers = 2U;         // requests running at the same time
    std::size_t maxQueued = 32U;      // requests waiting for a worker; beyond this Send fails at once
    std::size_t maxCompleted = 256U;  // answers waiting for Poll
};

using ResponseCallback = std::function<void(Response&&)>;

class Client final {
public:
    explicit Client(ClientLimits limits = {});
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client(); // cancels what is running, drops what is queued and joins the workers

    // Queues the request. An invalid request, a full queue or an unsupported platform still reports through
    // `callback` (from Poll), so the caller has one place to handle the outcome.
    RequestHandle Send(Request request, ResponseCallback callback);
    // Runs the callbacks of finished requests on this thread; returns how many ran.
    std::size_t Poll();
    // Requests queued or running.
    [[nodiscard]] std::size_t Pending() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// One request on the calling thread, redirects and all. For tools and tests; games use Client.
[[nodiscard]] Response Perform(Request request, const std::shared_ptr<CancelToken>& token = nullptr);

// Used by a backend: the cancellation flag of one request, and a hook that aborts the call in progress.
class CancelToken {
public:
    void Cancel();
    [[nodiscard]] bool Cancelled() const noexcept { return cancelled_.load(); }
    void SetAbort(std::function<void()> abort);
    void ClearAbort();
    void MarkFinished() noexcept { finished_.store(true); }
    [[nodiscard]] bool Finished() const noexcept { return finished_.load(); }

private:
    std::atomic<bool> cancelled_{ false };
    std::atomic<bool> finished_{ false };
    std::function<void()> abort_;
    mutable std::mutex mutex_;
};

} // namespace kb::http
