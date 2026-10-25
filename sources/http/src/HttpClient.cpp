#include "kb/http/HttpClient.hpp"

#include "Backend.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>

namespace kb::http {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxUrlBytes = 2048U;
constexpr std::size_t kMaxHeaders = 64U;
constexpr std::size_t kMaxHeaderBytes = 8192U;
constexpr std::size_t kMaxBodyBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaxResponseLimit = 256U * 1024U * 1024U;

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] bool IsToken(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    return std::ranges::all_of(text, [](unsigned char c) { return std::isalnum(c) != 0 || std::string_view{ "!#$%&'*+-.^_`|~" }.find(static_cast<char>(c)) != std::string_view::npos; });
}

[[nodiscard]] bool IsHex64(const std::string& text) {
    return text.size() == 64U && std::ranges::all_of(text, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

[[nodiscard]] bool SameOrigin(const detail::ParsedUrl& left, const detail::ParsedUrl& right) {
    return left.https == right.https && left.host == right.host && left.port == right.port;
}

// Headers the library sets itself, and that a caller must not be able to forge.
[[nodiscard]] bool IsReservedHeader(const std::string& lowerName) {
    return lowerName == "host" || lowerName == "content-length" || lowerName == "transfer-encoding" || lowerName == "connection" || lowerName == "upgrade" ||
        lowerName == "expect" || lowerName == "te" || lowerName == "trailer";
}

[[nodiscard]] bool MethodAllowsBody(const std::string& method) {
    return method == "POST" || method == "PUT" || method == "PATCH";
}

[[nodiscard]] std::string ResolveLocation(const detail::ParsedUrl& base, const std::string& location) {
    if (location.rfind("//", 0U) == 0U) {
        return std::string{ base.https ? "https:" : "http:" } + location;
    }
    if (location.find("://") != std::string::npos) {
        return location;
    }
    const std::string origin = std::string{ base.https ? "https://" : "http://" } + base.host + ((base.https ? base.port != 443U : base.port != 80U) ? ":" + std::to_string(base.port) : "");
    if (!location.empty() && location.front() == '/') {
        return origin + location;
    }
    // A relative reference: resolve against the directory of the current path, removing . and .. segments.
    std::string path = base.target;
    path = path.substr(0U, path.find('?'));
    path = path.substr(0U, path.rfind('/') + 1U);
    std::string joined = path + location;
    std::string query;
    if (const std::size_t mark = joined.find('?'); mark != std::string::npos) {
        query = joined.substr(mark);
        joined.erase(mark);
    }
    std::vector<std::string> segments;
    std::size_t position = 0U;
    while (position <= joined.size()) {
        std::size_t slash = joined.find('/', position);
        if (slash == std::string::npos) {
            slash = joined.size();
        }
        const std::string segment = joined.substr(position, slash - position);
        if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        position = slash + 1U;
    }
    std::string result = "/";
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        result += segments[index] + (index + 1U < segments.size() ? "/" : "");
    }
    if (!joined.empty() && joined.back() == '/' && result.back() != '/') {
        result += '/';
    }
    return origin + result + query;
}

} // namespace

namespace detail {

bool ParseUrl(const std::string& url, ParsedUrl& parsed, std::string& error) {
    if (url.empty() || url.size() > kMaxUrlBytes) {
        error = "the URL is empty or longer than 2048 bytes";
        return false;
    }
    if (std::ranges::any_of(url, [](unsigned char c) { return c <= 0x20U || c == 0x7FU; })) {
        error = "the URL has a space or a control character; encode it";
        return false;
    }
    std::string text = url;
    if (const std::size_t fragment = text.find('#'); fragment != std::string::npos) {
        text.erase(fragment);
    }
    const std::string lower = Lower(text.substr(0U, 8U));
    std::size_t start = 0U;
    if (lower.rfind("https://", 0U) == 0U) {
        parsed.https = true;
        start = 8U;
    } else if (lower.rfind("http://", 0U) == 0U) {
        parsed.https = false;
        start = 7U;
    } else {
        error = "only http:// and https:// URLs are supported";
        return false;
    }
    std::size_t end = text.find_first_of("/?", start);
    if (end == std::string::npos) {
        end = text.size();
    }
    std::string authority = text.substr(start, end - start);
    if (authority.find('@') != std::string::npos) {
        error = "a URL with a user name or password is refused; send credentials in a header";
        return false;
    }
    if (!authority.empty() && authority.front() == '[') {
        error = "IPv6 literal addresses are not supported";
        return false;
    }
    parsed.port = parsed.https ? 443U : 80U;
    if (const std::size_t colon = authority.rfind(':'); colon != std::string::npos) {
        const std::string portText = authority.substr(colon + 1U);
        if (portText.empty() || portText.size() > 5U || !std::ranges::all_of(portText, [](unsigned char c) { return std::isdigit(c) != 0; })) {
            error = "the port in the URL is not a number";
            return false;
        }
        const unsigned long port = std::stoul(portText);
        if (port == 0UL || port > 65535UL) {
            error = "the port in the URL is out of range";
            return false;
        }
        parsed.port = static_cast<std::uint16_t>(port);
        authority.erase(colon);
    }
    if (authority.empty() || authority.size() > 253U) {
        error = "the URL has no host";
        return false;
    }
    for (const char c : authority) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '.' || c == '_')) {
            error = "the host in the URL has a character a host name cannot have";
            return false;
        }
    }
    parsed.host = Lower(authority);
    parsed.target = end < text.size() ? text.substr(end) : "/";
    if (parsed.target.front() == '?') {
        parsed.target = "/" + parsed.target;
    }
    parsed.text = text;
    return true;
}

} // namespace detail

bool ValidateRequest(const Request& request, std::string& error) {
    static const char* const kMethods[] = { "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE" };
    if (std::ranges::find(kMethods, request.method) == std::end(kMethods)) {
        error = "the method must be GET, HEAD, POST, PUT, PATCH or DELETE";
        return false;
    }
    detail::ParsedUrl url;
    if (!detail::ParseUrl(request.url, url, error)) {
        return false;
    }
    if (request.headers.size() > kMaxHeaders) {
        error = "too many headers (at most 64)";
        return false;
    }
    for (const Header& header : request.headers) {
        if (!IsToken(header.name)) {
            error = "a header name is not valid";
            return false;
        }
        if (IsReservedHeader(Lower(header.name))) {
            error = "the header '" + header.name + "' is set by the library";
            return false;
        }
        if (header.value.size() > kMaxHeaderBytes || std::ranges::any_of(header.value, [](unsigned char c) { return c == '\r' || c == '\n' || c == 0U; })) {
            error = "the value of header '" + header.name + "' is too long or has a line break";
            return false;
        }
    }
    if (!request.body.empty() && !MethodAllowsBody(request.method)) {
        error = "only POST, PUT and PATCH carry a body";
        return false;
    }
    if (request.body.size() > kMaxBodyBytes) {
        error = "the body is larger than 16 MiB";
        return false;
    }
    if (request.timeoutMilliseconds < 50U || request.timeoutMilliseconds > 600'000U) {
        error = "the timeout must be between 50 ms and 10 minutes";
        return false;
    }
    if (request.maxResponseBytes == 0U || request.maxResponseBytes > kMaxResponseLimit) {
        error = "the response limit must be between 1 byte and 256 MiB";
        return false;
    }
    if (request.maxRedirects > 20U) {
        error = "at most 20 redirects can be allowed";
        return false;
    }
    if (!request.pinnedCertificateSha256.empty() && !IsHex64(request.pinnedCertificateSha256)) {
        error = "the pinned certificate must be 64 hexadecimal digits (SHA-256)";
        return false;
    }
    if (!url.https && !request.allowInsecureHttp) {
        error = "http:// is refused: use https://, or set allowInsecureHttp on purpose";
        return false;
    }
    return true;
}

void CancelToken::Cancel() {
    cancelled_.store(true);
    std::function<void()> abort;
    {
        const std::lock_guard lock{ mutex_ };
        abort = abort_;
    }
    if (abort) {
        abort();
    }
}

void CancelToken::SetAbort(std::function<void()> abort) {
    const std::lock_guard lock{ mutex_ };
    abort_ = std::move(abort);
}

void CancelToken::ClearAbort() {
    const std::lock_guard lock{ mutex_ };
    abort_ = nullptr;
}

void RequestHandle::Cancel() const {
    if (state_ != nullptr) {
        state_->Cancel();
    }
}

bool RequestHandle::Finished() const {
    return state_ == nullptr || state_->Finished();
}

Response Perform(Request request, const std::shared_ptr<CancelToken>& externalToken) {
    const auto started = Clock::now();
    const auto elapsed = [&] { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()); };
    const std::shared_ptr<CancelToken> token = externalToken != nullptr ? externalToken : std::make_shared<CancelToken>();
    Response response;
    const auto finish = [&](Status status, std::string error) {
        response.status = status;
        response.error = std::move(error);
        response.elapsedMilliseconds = elapsed();
        token->MarkFinished();
        return response;
    };

    std::string error;
    if (!ValidateRequest(request, error)) {
        // An http:// URL that is refused only for its scheme is reported as a policy refusal.
        return finish(error.rfind("http:// is refused", 0U) == 0U ? Status::InsecureBlocked : Status::InvalidRequest, error);
    }
    detail::ParsedUrl url;
    static_cast<void>(detail::ParseUrl(request.url, url, error));
    std::string method = request.method;
    std::vector<Header> headers = request.headers;
    std::vector<std::uint8_t> body = request.body;

    for (;;) {
        if (token->Cancelled()) {
            return finish(Status::Cancelled, "the request was cancelled");
        }
        const std::uint64_t used = elapsed();
        if (used >= request.timeoutMilliseconds) {
            return finish(Status::Timeout, "the request took longer than " + std::to_string(request.timeoutMilliseconds) + " ms");
        }
        detail::HopRequest hop;
        hop.method = method;
        hop.url = url;
        hop.headers = headers;
        hop.body = body.empty() ? nullptr : &body;
        hop.timeoutMilliseconds = static_cast<std::uint32_t>(request.timeoutMilliseconds - used);
        hop.maxResponseBytes = request.maxResponseBytes;
        hop.pinnedCertificateSha256 = request.pinnedCertificateSha256;
        detail::HopResponse answer = detail::PerformHop(hop, *token);
        response.finalUrl = url.text;
        if (answer.status != Status::Ok) {
            response.httpStatus = answer.httpStatus;
            return finish(answer.status, answer.error);
        }
        response.httpStatus = answer.httpStatus;
        response.headers = std::move(answer.headers);
        response.body = std::move(answer.body);

        const bool redirect = answer.httpStatus == 301 || answer.httpStatus == 302 || answer.httpStatus == 303 || answer.httpStatus == 307 || answer.httpStatus == 308;
        std::string location;
        if (redirect) {
            for (const Header& header : response.headers) {
                if (Lower(header.name) == "location") {
                    location = header.value;
                }
            }
        }
        if (!redirect || location.empty()) {
            return finish(Status::Ok, {});
        }
        if (response.redirects >= request.maxRedirects) {
            return finish(Status::TooManyRedirects, "more than " + std::to_string(request.maxRedirects) + " redirects");
        }
        detail::ParsedUrl next;
        if (!detail::ParseUrl(ResolveLocation(url, location), next, error)) {
            return finish(Status::RedirectRefused, "the redirect target is not acceptable: " + error);
        }
        if (url.https && !next.https) {
            return finish(Status::InsecureBlocked, "a redirect from https to http was refused");
        }
        if (!next.https && !request.allowInsecureHttp) {
            return finish(Status::InsecureBlocked, "a redirect to http:// was refused");
        }
        if (!SameOrigin(url, next)) {
            std::erase_if(headers, [](const Header& header) {
                const std::string name = Lower(header.name);
                return name == "authorization" || name == "cookie" || name == "proxy-authorization";
            });
        }
        if (answer.httpStatus == 303 || ((answer.httpStatus == 301 || answer.httpStatus == 302) && method == "POST")) {
            if (method != "HEAD") {
                method = "GET";
            }
            body.clear();
        }
        url = std::move(next);
        ++response.redirects;
        response.body.clear();
        response.headers.clear();
    }
}

class Client::Impl {
public:
    explicit Impl(ClientLimits limits) : limits_(limits) {
        const std::size_t workers = std::clamp<std::size_t>(limits_.workers, 1U, 16U);
        for (std::size_t index = 0U; index < workers; ++index) {
            threads_.emplace_back([this] { Work(); });
        }
    }

    ~Impl() {
        std::vector<std::shared_ptr<CancelToken>> running;
        {
            const std::lock_guard lock{ mutex_ };
            stopping_ = true;
            queue_.clear();
            running = active_;
        }
        for (const std::shared_ptr<CancelToken>& token : running) {
            token->Cancel();
        }
        wake_.notify_all();
        for (std::thread& thread : threads_) {
            thread.join();
        }
    }

    RequestHandle Send(Request request, ResponseCallback callback) {
        auto token = std::make_shared<CancelToken>();
        const std::lock_guard lock{ mutex_ };
        if (stopping_ || queue_.size() >= limits_.maxQueued) {
            Response refused;
            refused.status = Status::Error;
            refused.error = "too many requests are waiting";
            refused.finalUrl = request.url;
            token->MarkFinished();
            Complete(std::move(callback), std::move(refused));
            return RequestHandle{ token };
        }
        queue_.push_back(Task{ std::move(request), std::move(callback), token });
        ++pending_;
        wake_.notify_one();
        return RequestHandle{ token };
    }

    std::size_t Poll() {
        std::deque<Done> done;
        {
            const std::lock_guard lock{ mutex_ };
            done.swap(completed_);
        }
        for (Done& item : done) {
            if (item.callback) {
                item.callback(std::move(item.response));
            }
        }
        return done.size();
    }

    std::size_t Pending() const {
        const std::lock_guard lock{ mutex_ };
        return pending_;
    }

private:
    struct Task {
        Request request;
        ResponseCallback callback;
        std::shared_ptr<CancelToken> token;
    };
    struct Done {
        ResponseCallback callback;
        Response response;
    };

    // Called with the mutex held.
    void Complete(ResponseCallback callback, Response response) {
        if (completed_.size() >= limits_.maxCompleted) {
            completed_.pop_front(); // a caller that never polls does not grow memory without bound
        }
        completed_.push_back(Done{ std::move(callback), std::move(response) });
    }

    void Work() {
        for (;;) {
            Task task;
            {
                std::unique_lock lock{ mutex_ };
                wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_) {
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
                active_.push_back(task.token);
            }
            Response response = Perform(std::move(task.request), task.token);
            task.token->MarkFinished();
            {
                const std::lock_guard lock{ mutex_ };
                std::erase(active_, task.token);
                --pending_;
                Complete(std::move(task.callback), std::move(response));
            }
        }
    }

    ClientLimits limits_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> queue_;
    std::deque<Done> completed_;
    std::vector<std::shared_ptr<CancelToken>> active_;
    std::vector<std::thread> threads_;
    std::size_t pending_ = 0U;
    bool stopping_ = false;
};

Client::Client(ClientLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
Client::~Client() = default;
RequestHandle Client::Send(Request request, ResponseCallback callback) {
    return impl_->Send(std::move(request), std::move(callback));
}
std::size_t Client::Poll() {
    return impl_->Poll();
}
std::size_t Client::Pending() const {
    return impl_->Pending();
}

} // namespace kb::http
