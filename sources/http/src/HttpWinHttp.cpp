#include "Backend.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace kb::http::detail {

namespace {

using Clock = std::chrono::steady_clock;

constexpr DWORD kTlsProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000UL; // TLS 1.2 and TLS 1.3 only

[[nodiscard]] std::wstring Wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

[[nodiscard]] std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

// Two sessions, so that a connection made under the relaxed checks of a pinned certificate can never be
// reused by a request that relies on the system's checks: each session has its own connections.
[[nodiscard]] HINTERNET Session(bool pinned) {
    static const HINTERNET systemSession = [] {
        HINTERNET handle = WinHttpOpen(L"21kb-http/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (handle != nullptr) {
            DWORD protocols = kTlsProtocols;
            WinHttpSetOption(handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
        return handle;
    }();
    static const HINTERNET pinnedSession = [] {
        HINTERNET handle = WinHttpOpen(L"21kb-http/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (handle != nullptr) {
            DWORD protocols = kTlsProtocols;
            WinHttpSetOption(handle, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
        return handle;
    }();
    return pinned ? pinnedSession : systemSession;
}

// Ends the call in progress when the deadline of the whole hop has passed. WinHTTP's own timeouts are per
// operation and per attempt, and retry silently, so they alone do not bound the request.
class Watchdog {
public:
    Watchdog(std::function<void()> abort, int milliseconds) : abort_(std::move(abort)) {
        thread_ = std::thread([this, milliseconds] {
            std::unique_lock lock{ mutex_ };
            if (!wake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return done_; })) {
                fired_.store(true);
                lock.unlock();
                abort_();
            }
        });
    }
    ~Watchdog() {
        {
            const std::lock_guard lock{ mutex_ };
            done_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    [[nodiscard]] bool Fired() const noexcept { return fired_.load(); }

private:
    std::function<void()> abort_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool done_ = false;
    std::atomic<bool> fired_{ false };
    std::thread thread_;
};

[[nodiscard]] std::string Sha256Hex(const BYTE* data, DWORD size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return {};
    }
    std::array<UCHAR, 32> digest{};
    const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data), size, digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        return {};
    }
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    for (const UCHAR byte : digest) {
        hex.push_back(kDigits[byte >> 4U]);
        hex.push_back(kDigits[byte & 0x0FU]);
    }
    return hex;
}

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// One request handle that can be closed from another thread exactly once.
class SharedRequest {
public:
    explicit SharedRequest(HINTERNET handle) : handle_(handle) {}
    ~SharedRequest() { Close(); }
    SharedRequest(const SharedRequest&) = delete;
    SharedRequest& operator=(const SharedRequest&) = delete;

    [[nodiscard]] HINTERNET Get() const noexcept { return handle_; }
    void Close() {
        const std::lock_guard lock{ mutex_ };
        if (handle_ != nullptr && !closed_) {
            closed_ = true;
            WinHttpCloseHandle(handle_);
        }
    }

private:
    HINTERNET handle_;
    bool closed_ = false;
    std::mutex mutex_;
};

struct CallbackState {
    std::string pin;
    SharedRequest* request = nullptr;
    std::atomic<bool> pinChecked{ false };
    std::atomic<bool> pinMismatch{ false };
    std::atomic<DWORD> secureFailure{ 0U };
};

void CALLBACK StatusCallback(HINTERNET, DWORD_PTR context, DWORD status, LPVOID information, DWORD) {
    auto* state = reinterpret_cast<CallbackState*>(context);
    if (state == nullptr) {
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE && information != nullptr) {
        state->secureFailure.store(*static_cast<DWORD*>(information));
    } else if (status == WINHTTP_CALLBACK_STATUS_SENDING_REQUEST && !state->pin.empty() && !state->pinChecked.exchange(true)) {
        // The handshake is done and nothing of the request has been sent: this is the moment to check the pin.
        PCCERT_CONTEXT certificate = nullptr;
        DWORD size = static_cast<DWORD>(sizeof(PCCERT_CONTEXT));
        bool matches = false;
        if (WinHttpQueryOption(state->request->Get(), WINHTTP_OPTION_SERVER_CERT_CONTEXT, &certificate, &size) && certificate != nullptr) {
            matches = Lower(Sha256Hex(certificate->pbCertEncoded, certificate->cbCertEncoded)) == Lower(state->pin);
            CertFreeCertificateContext(certificate);
        }
        if (!matches) {
            state->pinMismatch.store(true);
            state->request->Close(); // the request is never sent
        }
    }
}

[[nodiscard]] std::string SecureFailureText(DWORD flags) {
    std::string text;
    const auto add = [&text](const char* part) {
        text += text.empty() ? "" : ", ";
        text += part;
    };
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA) != 0U) add("the certificate authority is not trusted");
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID) != 0U) add("the certificate is for another host name");
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID) != 0U) add("the certificate has expired or is not valid yet");
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED) != 0U) add("the certificate was revoked");
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED) != 0U) add("the revocation check failed");
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT) != 0U) add("the certificate is not valid");
    if ((flags & WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR) != 0U) add("the secure channel failed");
    return text.empty() ? "the secure connection could not be established" : text;
}

[[nodiscard]] Status StatusForError(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return Status::Timeout;
    case ERROR_WINHTTP_SECURE_FAILURE: return Status::CertificateError;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR: return Status::ConnectionFailed;
    default: return Status::Error;
    }
}

void ParseHeaders(const std::string& raw, std::vector<Header>& headers) {
    std::size_t position = raw.find("\r\n"); // skip the status line
    position = position == std::string::npos ? raw.size() : position + 2U;
    while (position < raw.size() && headers.size() < 100U) {
        std::size_t end = raw.find("\r\n", position);
        if (end == std::string::npos) {
            end = raw.size();
        }
        const std::string line = raw.substr(position, end - position);
        position = end + 2U;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0U) {
            continue;
        }
        std::string value = line.substr(colon + 1U);
        value.erase(0U, value.find_first_not_of(" \t"));
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
            value.pop_back();
        }
        headers.push_back(Header{ line.substr(0U, colon), std::move(value) });
    }
}

} // namespace

HopResponse PerformHop(const HopRequest& request, CancelToken& token) {
    HopResponse out;
    const auto started = Clock::now();
    const auto remaining = [&]() -> int {
        const auto used = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        return static_cast<int>(std::max<std::int64_t>(1, static_cast<std::int64_t>(request.timeoutMilliseconds) - used));
    };
    const auto fail = [&out](Status status, std::string message) {
        out.status = status;
        out.error = std::move(message);
        return out;
    };

    HINTERNET session = Session(!request.pinnedCertificateSha256.empty());
    if (session == nullptr) {
        return fail(Status::Error, "WinHTTP could not be opened (code " + std::to_string(GetLastError()) + ")");
    }
    const std::wstring host = Wide(request.url.host);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), request.url.port, 0);
    if (connection == nullptr) {
        return fail(Status::ConnectionFailed, "the connection could not be opened (code " + std::to_string(GetLastError()) + ")");
    }
    struct ConnectionCloser {
        HINTERNET handle;
        ~ConnectionCloser() { WinHttpCloseHandle(handle); }
    } connectionCloser{ connection };

    const std::wstring method = Wide(request.method);
    const std::wstring target = Wide(request.url.target);
    HINTERNET handle = WinHttpOpenRequest(connection, method.c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        request.url.https ? WINHTTP_FLAG_SECURE : 0U);
    if (handle == nullptr) {
        return fail(Status::Error, "the request could not be created (code " + std::to_string(GetLastError()) + ")");
    }
    SharedRequest shared(handle);
    CallbackState state;
    state.pin = request.pinnedCertificateSha256;
    state.request = &shared;
    token.SetAbort([&shared] { shared.Close(); });
    struct AbortClearer {
        CancelToken& token;
        ~AbortClearer() { token.ClearAbort(); }
    } abortClearer{ token };
    if (token.Cancelled()) {
        return fail(Status::Cancelled, "the request was cancelled");
    }
    Watchdog watchdog([&shared] { shared.Close(); }, remaining());

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER; // redirects are followed by the caller, one checked hop at a time
    WinHttpSetOption(handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH; // never send the user's Windows credentials on our own
    WinHttpSetOption(handle, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon));
    if (!state.pin.empty() && request.url.https) {
        // The pinned certificate replaces the chain and name checks; it is compared before anything is sent.
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(handle, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&state);
    WinHttpSetOption(handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context));
    WinHttpSetStatusCallback(handle, &StatusCallback, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE | WINHTTP_CALLBACK_FLAG_SEND_REQUEST, 0);
    const int budget = remaining();
    WinHttpSetTimeouts(handle, budget, budget, budget, budget);

    std::wstring headers;
    for (const Header& header : request.headers) {
        headers += Wide(header.name) + L": " + Wide(header.value) + L"\r\n";
    }
    const void* body = request.body != nullptr ? request.body->data() : WINHTTP_NO_REQUEST_DATA;
    const DWORD bodySize = request.body != nullptr ? static_cast<DWORD>(request.body->size()) : 0U;

    const auto finishFailure = [&](DWORD code) {
        if (state.pinMismatch.load()) {
            return fail(Status::CertificateError, "the server's certificate is not the pinned one");
        }
        if (watchdog.Fired()) {
            return fail(Status::Timeout, "the request took longer than its deadline");
        }
        if (token.Cancelled()) {
            return fail(Status::Cancelled, "the request was cancelled");
        }
        const Status status = StatusForError(code);
        if (status == Status::CertificateError) {
            return fail(status, SecureFailureText(state.secureFailure.load()));
        }
        if (status == Status::Timeout) {
            return fail(status, "the server did not answer in time");
        }
        return fail(status, "the request failed (WinHTTP code " + std::to_string(code) + ")");
    };

    if (!WinHttpSendRequest(handle, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0U : static_cast<DWORD>(-1L), const_cast<void*>(body), bodySize,
            bodySize, 0)) {
        return finishFailure(GetLastError());
    }
    if (!WinHttpReceiveResponse(handle, nullptr)) {
        return finishFailure(GetLastError());
    }
    if (state.pinMismatch.load()) {
        return finishFailure(ERROR_WINHTTP_OPERATION_CANCELLED);
    }

    DWORD statusCode = 0U;
    DWORD size = sizeof(statusCode);
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX)) {
        return finishFailure(GetLastError());
    }
    out.httpStatus = static_cast<int>(statusCode);
    DWORD rawSize = 0U;
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &rawSize, WINHTTP_NO_HEADER_INDEX);
    if (rawSize != 0U && rawSize <= 256U * 1024U) {
        std::wstring raw(rawSize / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(rawSize / sizeof(wchar_t));
            ParseHeaders(Narrow(raw), out.headers);
        }
    }
    for (const Header& header : out.headers) {
        if (Lower(header.name) == "content-length") {
            try {
                if (std::stoull(header.value) > request.maxResponseBytes) {
                    return fail(Status::TooLarge, "the response is larger than " + std::to_string(request.maxResponseBytes) + " bytes");
                }
            } catch (...) {
            }
        }
    }

    if (request.method != "HEAD") {
        for (;;) {
            if (token.Cancelled()) {
                return fail(Status::Cancelled, "the request was cancelled");
            }
            if (remaining() <= 1) {
                return fail(Status::Timeout, "the answer did not arrive in time");
            }
            const int budgetNow = remaining();
            WinHttpSetTimeouts(handle, budgetNow, budgetNow, budgetNow, budgetNow);
            DWORD available = 0U;
            if (!WinHttpQueryDataAvailable(handle, &available)) {
                return finishFailure(GetLastError());
            }
            if (available == 0U) {
                break;
            }
            if (out.body.size() + available > request.maxResponseBytes) {
                return fail(Status::TooLarge, "the response is larger than " + std::to_string(request.maxResponseBytes) + " bytes");
            }
            const std::size_t offset = out.body.size();
            out.body.resize(offset + available);
            DWORD read = 0U;
            if (!WinHttpReadData(handle, out.body.data() + offset, available, &read)) {
                return finishFailure(GetLastError());
            }
            out.body.resize(offset + read);
            if (read == 0U) {
                break;
            }
        }
    }
    out.status = Status::Ok;
    return out;
}

} // namespace kb::http::detail
