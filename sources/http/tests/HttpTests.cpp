// Tests for kb_http against a local server (tests/http_test_server.py, started with Python) over plain
// HTTP and over HTTPS with a throwaway self-signed certificate (made with openssl): redirects, limits,
// timeouts, cancellation, certificate errors and pinning, and the checks that need no network.

#include "kb/http/HttpClient.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef KB_HTTP_TEST_SERVER
#error KB_HTTP_TEST_SERVER must name tests/http_test_server.py
#endif

namespace {

using namespace kb::http;
namespace fs = std::filesystem;

[[maybe_unused]] constexpr int kSkipped = 77; // CTest's SKIP_RETURN_CODE

[[maybe_unused]] void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[maybe_unused]] std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

[[maybe_unused]] std::string Text(const Response& response) {
    return std::string(response.body.begin(), response.body.end());
}

[[maybe_unused]] bool WaitFor(const std::function<bool()>& condition, int timeoutMilliseconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

#if defined(_WIN32)

// Runs a command line; with `wait` returns its exit code, otherwise the handle is kept in `process`.
int Run(const std::wstring& commandLine, const fs::path& output, HANDLE* process, bool wait) {
    SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE out = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = out;
    startup.hStdError = out;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION info{};
    std::wstring mutableLine = commandLine;
    const BOOL created = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    CloseHandle(out);
    if (!created) {
        return -1;
    }
    CloseHandle(info.hThread);
    if (!wait) {
        *process = info.hProcess;
        return 0;
    }
    WaitForSingleObject(info.hProcess, 60'000);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}

fs::path FindOpenssl() {
    for (const char* candidate : { "C:/Program Files/Git/usr/bin/openssl.exe", "C:/Program Files/Git/mingw64/bin/openssl.exe", "C:/Program Files (x86)/Git/usr/bin/openssl.exe" }) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

struct Fixture {
    fs::path dir;
    HANDLE server = nullptr;
    int httpPort = 0;
    int httpsPort = 0;
    std::string fingerprint; // SHA-256 of the certificate, lower case hex

    ~Fixture() {
        if (server != nullptr) {
            TerminateProcess(server, 0);
            CloseHandle(server);
        }
    }

    [[nodiscard]] std::string Url(bool secure, const std::string& path) const {
        return std::string{ secure ? "https://127.0.0.1:" : "http://127.0.0.1:" } + std::to_string(secure ? httpsPort : httpPort) + path;
    }

    [[nodiscard]] std::size_t Logged(const std::string& text) const {
        const std::string log = ReadFile(dir / "requests.log");
        std::size_t count = 0U;
        for (std::size_t position = log.find(text); position != std::string::npos; position = log.find(text, position + 1U)) {
            ++count;
        }
        return count;
    }
};

// Returns false (and says why) when the environment cannot run the tests.
bool StartFixture(Fixture& fixture, std::string& why) {
    const fs::path openssl = FindOpenssl();
    if (openssl.empty()) {
        why = "openssl was not found (Git for Windows ships it)";
        return false;
    }
    fixture.dir = fs::temp_directory_path() / ("21kb_http_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(fixture.dir);
    const fs::path cert = fixture.dir / "cert.pem";
    const fs::path key = fixture.dir / "key.pem";
    const std::wstring generate = L"\"" + openssl.wstring() + L"\" req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=localhost -addext subjectAltName=DNS:localhost,IP:127.0.0.1 -keyout \"" +
        key.wstring() + L"\" -out \"" + cert.wstring() + L"\"";
    if (Run(generate, fixture.dir / "openssl.log", nullptr, true) != 0 || !fs::exists(cert)) {
        why = "openssl could not make a certificate: " + ReadFile(fixture.dir / "openssl.log");
        return false;
    }
    const std::wstring fingerprintCommand = L"\"" + openssl.wstring() + L"\" x509 -in \"" + cert.wstring() + L"\" -noout -fingerprint -sha256";
    if (Run(fingerprintCommand, fixture.dir / "fingerprint.txt", nullptr, true) != 0) {
        why = "openssl could not read the certificate";
        return false;
    }
    const std::string line = ReadFile(fixture.dir / "fingerprint.txt");
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
        why = "the fingerprint line is not understood: " + line;
        return false;
    }
    for (std::size_t index = equals + 1U; index < line.size(); ++index) {
        if (std::isxdigit(static_cast<unsigned char>(line[index])) != 0) {
            fixture.fingerprint.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(line[index]))));
        }
    }
    const std::wstring serve = L"python \"" + fs::path(KB_HTTP_TEST_SERVER).wstring() + L"\" \"" + fixture.dir.wstring() + L"\" \"" + cert.wstring() + L"\" \"" + key.wstring() + L"\"";
    if (Run(serve, fixture.dir / "server.log", &fixture.server, false) != 0) {
        why = "python could not be started";
        return false;
    }
    if (!WaitFor([&] { return fs::exists(fixture.dir / "ports.json"); }, 20'000)) {
        why = "the test server did not start: " + ReadFile(fixture.dir / "server.log");
        return false;
    }
    const std::string ports = ReadFile(fixture.dir / "ports.json");
    const auto number = [&ports](const char* name) {
        const std::size_t at = ports.find(std::string{ "\"" } + name + "\":");
        return at == std::string::npos ? 0 : std::stoi(ports.substr(at + std::string{ name }.size() + 3U));
    };
    fixture.httpPort = number("http");
    fixture.httpsPort = number("https");
    return fixture.httpPort != 0 && fixture.httpsPort != 0 && fixture.fingerprint.size() == 64U;
}

Request Get(const std::string& url) {
    Request request;
    request.url = url;
    request.allowInsecureHttp = true;
    request.timeoutMilliseconds = 8000U;
    return request;
}

const Header* FindHeader(const Response& response, const std::string& name) {
    for (const Header& header : response.headers) {
        if (header.name.size() == name.size() && std::equal(name.begin(), name.end(), header.name.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
            return &header;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------------ tests

void TestRequestChecks() {
    Request request;
    request.url = "https://example.com/";
    std::string error;
    Require(ValidateRequest(request, error), "a plain https request was refused: " + error);
    const auto refused = [&](Request changed, const char* text, const char* why) {
        std::string message;
        Require(!ValidateRequest(changed, message), std::string{ "accepted: " } + why);
        Require(message.find(text) != std::string::npos, std::string{ "the reason for '" } + why + "' is unclear: " + message);
    };
    const auto with = [&](const std::function<void(Request&)>& change) {
        Request changed = request;
        change(changed);
        return changed;
    };
    refused(with([](Request& r) { r.url = ""; }), "URL", "an empty URL");
    refused(with([](Request& r) { r.url = "file:///c:/windows/win.ini"; }), "http://", "a file URL");
    refused(with([](Request& r) { r.url = "ftp://example.com/"; }), "http://", "an ftp URL");
    refused(with([](Request& r) { r.url = "javascript:alert(1)"; }), "http://", "a javascript URL");
    refused(with([](Request& r) { r.url = "https://user:secret@example.com/"; }), "user name", "credentials in the URL");
    refused(with([](Request& r) { r.url = "https://example.com/a b"; }), "space", "a space in the URL");
    refused(with([](Request& r) { r.url = "https://example.com/\r\nX: y"; }), "control", "a line break in the URL");
    refused(with([](Request& r) { r.url = "https://example.com:99999/"; }), "port", "a port out of range");
    refused(with([](Request& r) { r.url = "https://example.com:abc/"; }), "port", "a port that is not a number");
    refused(with([](Request& r) { r.url = "https:///path"; }), "host", "no host");
    refused(with([](Request& r) { r.url = "https://[::1]/"; }), "IPv6", "an IPv6 literal");
    refused(with([](Request& r) { r.url = "https://exa mple.com/"; }), "space", "a space in the host");
    refused(with([](Request& r) { r.url = "https://exa$mple.com/"; }), "host", "a bad character in the host");
    refused(with([](Request& r) { r.url = "https://example.com/" + std::string(3000U, 'a'); }), "2048", "a long URL");
    refused(with([](Request& r) { r.url = "http://example.com/"; }), "http:// is refused", "plain http without permission");
    refused(with([](Request& r) { r.method = "TRACE"; }), "method", "an unknown method");
    refused(with([](Request& r) { r.method = "get"; }), "method", "a lower case method");
    refused(with([](Request& r) { r.headers.push_back({ "X-Test", "a\r\nInjected: yes" }); }), "line break", "header injection in a value");
    refused(with([](Request& r) { r.headers.push_back({ "X Test", "a" }); }), "name", "a space in a header name");
    refused(with([](Request& r) { r.headers.push_back({ "Host", "evil.example" }); }), "set by the library", "a Host header");
    refused(with([](Request& r) { r.headers.push_back({ "Content-Length", "5" }); }), "set by the library", "a Content-Length header");
    refused(with([](Request& r) { r.headers.push_back({ "transfer-encoding", "chunked" }); }), "set by the library", "a Transfer-Encoding header");
    refused(with([](Request& r) { r.body = { 1U, 2U }; }), "body", "a body on GET");
    refused(with([](Request& r) { r.timeoutMilliseconds = 1U; }), "timeout", "a tiny timeout");
    refused(with([](Request& r) { r.timeoutMilliseconds = 10'000'000U; }), "timeout", "a huge timeout");
    refused(with([](Request& r) { r.maxResponseBytes = 0U; }), "limit", "a zero response limit");
    refused(with([](Request& r) { r.maxRedirects = 100U; }), "redirects", "too many redirects allowed");
    refused(with([](Request& r) { r.pinnedCertificateSha256 = "abc"; }), "pinned", "a short pin");
    refused(with([](Request& r) { r.pinnedCertificateSha256 = std::string(64U, 'g'); }), "pinned", "a pin that is not hexadecimal");
    refused(with([](Request& r) { r.headers.assign(70U, Header{ "X-A", "b" }); }), "headers", "too many headers");
    Request post = request;
    post.method = "POST";
    post.body = { 1U, 2U, 3U };
    Require(ValidateRequest(post, error), "a POST with a body was refused");

    // The same checks are what Perform reports, without touching the network.
    request.url = "http://example.com/";
    Response blocked = Perform(request);
    Require(blocked.status == Status::InsecureBlocked, "plain http was not reported as a policy refusal");
    request.url = "ftp://example.com/";
    Require(Perform(request).status == Status::InvalidRequest, "an ftp URL was not reported as invalid");
}

void TestPlainRequests(const Fixture& fixture) {
    const Response hello = Perform(Get(fixture.Url(false, "/hello")));
    Require(hello.status == Status::Ok && hello.httpStatus == 200 && Text(hello) == "hello" && hello.redirects == 0U, "GET /hello: " + hello.error);
    Require(FindHeader(hello, "X-Test") != nullptr && FindHeader(hello, "x-test")->value == "yes", "a response header was lost");
    Require(hello.finalUrl == fixture.Url(false, "/hello"), "the final URL is wrong");

    Request head = Get(fixture.Url(false, "/hello"));
    head.method = "HEAD";
    const Response headed = Perform(head);
    Require(headed.status == Status::Ok && headed.httpStatus == 200 && headed.body.empty(), "HEAD returned a body");

    Request post = Get(fixture.Url(false, "/echo"));
    post.method = "POST";
    post.body = { 'p', 'i', 'n', 'g' };
    post.headers.push_back({ "Authorization", "Bearer token" });
    const Response echoed = Perform(post);
    Require(echoed.status == Status::Ok && Text(echoed) == "ping" && FindHeader(echoed, "X-Method")->value == "POST" && FindHeader(echoed, "X-Auth")->value == "Bearer token",
        "POST /echo: " + echoed.error);
    post.method = "PUT";
    Require(Text(Perform(post)) == "ping", "PUT did not carry its body");
    Request remove = Get(fixture.Url(false, "/echo"));
    remove.method = "DELETE";
    Require(FindHeader(Perform(remove), "X-Method")->value == "DELETE", "DELETE was not sent as DELETE");

    const Response missing = Perform(Get(fixture.Url(false, "/status/404")));
    Require(missing.status == Status::Ok && missing.httpStatus == 404, "a 404 is an answer, not a failure of the library");
    const Response failing = Perform(Get(fixture.Url(false, "/status/500")));
    Require(failing.status == Status::Ok && failing.httpStatus == 500, "a 500 is an answer, not a failure of the library");

    Request agent = Get(fixture.Url(false, "/headers"));
    agent.headers.push_back({ "X-Custom", "one" });
    const std::string seen = Text(Perform(agent));
    Require(seen.find("X-Custom: one") != std::string::npos, "a custom header did not arrive");

    // Nothing listens there; nobody answers that name.
    Request closed = Get("http://127.0.0.1:1/");
    closed.timeoutMilliseconds = 5000U;
    Require(Perform(closed).status == Status::ConnectionFailed, "a closed port was not reported as a failed connection");
    Request nowhere = Get("http://no-such-host.invalid/");
    nowhere.timeoutMilliseconds = 8000U;
    const Status unknown = Perform(nowhere).status;
    Require(unknown == Status::ConnectionFailed || unknown == Status::Timeout, "an unknown host was not reported as a failed connection");
}

void TestRedirects(const Fixture& fixture) {
    const Response redirected = Perform(Get(fixture.Url(false, "/redirect")));
    Require(redirected.status == Status::Ok && Text(redirected) == "hello" && redirected.redirects == 1U && redirected.finalUrl == fixture.Url(false, "/hello"), "a 302 was not followed: " + redirected.error);
    const Response relative = Perform(Get(fixture.Url(false, "/relative")));
    Require(relative.status == Status::Ok && Text(relative) == "hello" && relative.finalUrl == fixture.Url(false, "/hello"), "a relative redirect was not resolved");

    Request post = Get(fixture.Url(false, "/redirect303"));
    post.method = "POST";
    post.body = { 'b', 'o', 'd', 'y' };
    const Response seeOther = Perform(post);
    Require(seeOther.status == Status::Ok && FindHeader(seeOther, "X-Method")->value == "GET" && seeOther.body.empty(), "a 303 did not turn the POST into a GET");
    post.url = fixture.Url(false, "/redirect307");
    const Response temporary = Perform(post);
    Require(temporary.status == Status::Ok && FindHeader(temporary, "X-Method")->value == "POST" && Text(temporary) == "body", "a 307 did not keep the method and body");

    Request loop = Get(fixture.Url(false, "/loop"));
    loop.maxRedirects = 3U;
    const Response looped = Perform(loop);
    Require(looped.status == Status::TooManyRedirects && looped.redirects == 3U, "a redirect loop was not stopped");
    Request none = Get(fixture.Url(false, "/redirect"));
    none.maxRedirects = 0U;
    Require(Perform(none).status == Status::TooManyRedirects, "a redirect was followed with none allowed");

    Require(Perform(Get(fixture.Url(false, "/to?url=file:///c:/windows/win.ini"))).status == Status::RedirectRefused, "a redirect to a file URL was followed");
    Require(Perform(Get(fixture.Url(false, "/to?url=ftp://example.com/"))).status == Status::RedirectRefused, "a redirect to ftp was followed");
    Request insecure = Get(fixture.Url(false, "/redirect"));
    insecure.allowInsecureHttp = false;
    insecure.url = fixture.Url(false, "/redirect");
    Require(Perform(insecure).status == Status::InsecureBlocked, "plain http was reached without permission");

    // Credentials stay with the host they were meant for.
    Request cross = Get(fixture.Url(false, "/to?url=http://localhost:" + std::to_string(fixture.httpPort) + "/headers"));
    cross.headers.push_back({ "Authorization", "Bearer secret" });
    cross.headers.push_back({ "X-Keep", "yes" });
    const Response crossed = Perform(cross);
    Require(crossed.status == Status::Ok && crossed.redirects == 1U, "a cross host redirect failed: " + crossed.error);
    Require(Text(crossed).find("Authorization") == std::string::npos && Text(crossed).find("Bearer secret") == std::string::npos, "the Authorization header followed a redirect to another host");
    Require(Text(crossed).find("X-Keep: yes") != std::string::npos, "an ordinary header was dropped by a redirect");
    Request same = Get(fixture.Url(false, "/redirect307"));
    same.method = "POST";
    same.headers.push_back({ "Authorization", "Bearer secret" });
    same.body = { 'x' };
    Require(FindHeader(Perform(same), "X-Auth")->value == "Bearer secret", "the Authorization header was dropped by a redirect to the same host");

    // A secure page never sends the client to plain http, whatever is allowed.
    const std::size_t plainHelloBefore = fixture.Logged("http GET /hello");
    Request downgrade = Get(fixture.Url(true, "/to?url=http://127.0.0.1:" + std::to_string(fixture.httpPort) + "/hello"));
    downgrade.pinnedCertificateSha256 = fixture.fingerprint;
    const Response downgraded = Perform(downgrade);
    Require(downgraded.status == Status::InsecureBlocked, "a redirect from https to http was followed: " + downgraded.error);
    Require(fixture.Logged("http GET /hello") == plainHelloBefore, "the client reached the plain http server after a refused redirect");
}

void TestHttps(const Fixture& fixture) {
    Request pinned = Get(fixture.Url(true, "/hello"));
    pinned.pinnedCertificateSha256 = fixture.fingerprint;
    const Response ok = Perform(pinned);
    Require(ok.status == Status::Ok && Text(ok) == "hello", "a request to the pinned server failed: " + ok.error);

    pinned.method = "POST";
    pinned.url = fixture.Url(true, "/echo");
    pinned.body = { 's', 'e', 'c', 'r', 'e', 't' };
    std::string upper = fixture.fingerprint;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    pinned.pinnedCertificateSha256 = upper;
    Require(Text(Perform(pinned)) == "secret", "a pin in upper case was not accepted");

    // A pin that does not match: no answer is accepted and the request itself is never sent.
    const std::size_t before = fixture.Logged("https POST /echo");
    pinned.pinnedCertificateSha256 = std::string(64U, 'a');
    const Response wrong = Perform(pinned);
    Require(wrong.status == Status::CertificateError && wrong.body.empty(), "a wrong pin was accepted: " + wrong.error);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Require(fixture.Logged("https POST /echo") == before, "the request reached a server whose certificate did not match the pin");

    // Without a pin the system decides, and it does not know this certificate.
    const Response untrusted = Perform(Get(fixture.Url(true, "/hello")));
    Require(untrusted.status == Status::CertificateError && untrusted.body.empty() && !untrusted.error.empty(), "an untrusted certificate was accepted: " + untrusted.error);
    Require(untrusted.error.find("not trusted") != std::string::npos || untrusted.error.find("certificate") != std::string::npos, "the certificate error does not say what is wrong: " + untrusted.error);

    // Plain http where https is expected.
    Request wrongScheme = Get("https://127.0.0.1:" + std::to_string(fixture.httpPort) + "/hello");
    wrongScheme.pinnedCertificateSha256 = fixture.fingerprint;
    wrongScheme.timeoutMilliseconds = 3000U;
    const Status status = Perform(wrongScheme).status;
    Require(status != Status::Ok, "a plain http server answered a TLS request");
}

void TestTimeoutsAndLimits(const Fixture& fixture) {
    Request slow = Get(fixture.Url(false, "/slow"));
    slow.timeoutMilliseconds = 500U;
    const Response timedOut = Perform(slow);
    Require(timedOut.status == Status::Timeout && timedOut.elapsedMilliseconds < 2500U, "a slow server did not end in a timeout: " + std::to_string(timedOut.elapsedMilliseconds) + " ms " + timedOut.error);

    Request drip = Get(fixture.Url(false, "/drip"));
    drip.timeoutMilliseconds = 800U;
    const Response dripped = Perform(drip);
    Require(dripped.status == Status::Timeout && dripped.elapsedMilliseconds < 3000U, "a server that sends one byte at a time held the request past its deadline: " + std::to_string(dripped.elapsedMilliseconds));

    Request big = Get(fixture.Url(false, "/big?bytes=1000000"));
    big.maxResponseBytes = 100'000U;
    const Response tooBig = Perform(big);
    Require(tooBig.status == Status::TooLarge && tooBig.body.size() <= 100'000U, "a response over the limit was accepted");
    Request exact = Get(fixture.Url(false, "/big?bytes=5000"));
    exact.maxResponseBytes = 5000U;
    const Response fits = Perform(exact);
    Require(fits.status == Status::Ok && fits.body.size() == 5000U, "a response at the limit was refused");
    exact.maxResponseBytes = 4999U;
    Require(Perform(exact).status == Status::TooLarge, "a response one byte over the limit was accepted");
    Request stream = Get(fixture.Url(false, "/stream"));
    stream.maxResponseBytes = 50'000U;
    const Response streamed = Perform(stream);
    Require(streamed.status == Status::TooLarge && streamed.body.size() <= 50'000U && streamed.elapsedMilliseconds < 6000U, "an endless chunked response was not cut off: " + std::to_string(streamed.body.size()));
}

void TestAsyncClient(const Fixture& fixture) {
    // Callbacks run on the thread that polls.
    {
        Client client;
        std::vector<Response> answers;
        std::vector<std::thread::id> threads;
        for (int index = 0; index < 6; ++index) {
            client.Send(Get(fixture.Url(false, "/hello")), [&](Response&& response) {
                answers.push_back(std::move(response));
                threads.push_back(std::this_thread::get_id());
            });
        }
        Require(WaitFor([&] { static_cast<void>(client.Poll()); return answers.size() == 6U; }, 10'000), "not every request finished");
        for (std::size_t index = 0U; index < answers.size(); ++index) {
            Require(answers[index].status == Status::Ok && Text(answers[index]) == "hello" && threads[index] == std::this_thread::get_id(), "an answer is wrong or came on another thread");
        }
        Require(client.Pending() == 0U, "requests are still pending");
    }
    // Cancelling a request that is waiting for the server.
    {
        Client client;
        std::vector<Response> answers;
        Request slow = Get(fixture.Url(false, "/slow"));
        slow.timeoutMilliseconds = 20'000U;
        const RequestHandle handle = client.Send(slow, [&](Response&& response) { answers.push_back(std::move(response)); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto cancelled = std::chrono::steady_clock::now();
        handle.Cancel();
        Require(WaitFor([&] { static_cast<void>(client.Poll()); return !answers.empty(); }, 5000), "a cancelled request never finished");
        Require(answers.front().status == Status::Cancelled && std::chrono::steady_clock::now() - cancelled < std::chrono::milliseconds(2000), "a cancelled request did not end promptly as Cancelled");
        handle.Cancel(); // harmless once finished
        Require(handle.Finished(), "a finished request does not say so");
    }
    // A request cancelled before it started never reaches the server.
    {
        ClientLimits limits;
        limits.workers = 1U;
        Client client(limits);
        std::vector<Response> answers;
        Request slow = Get(fixture.Url(false, "/slow"));
        slow.timeoutMilliseconds = 20'000U;
        const RequestHandle running = client.Send(slow, [&](Response&& response) { answers.push_back(std::move(response)); });
        const std::size_t before = fixture.Logged("http GET /status/204");
        const RequestHandle waiting = client.Send(Get(fixture.Url(false, "/status/204")), [&](Response&& response) { answers.push_back(std::move(response)); });
        waiting.Cancel();
        running.Cancel();
        Require(WaitFor([&] { static_cast<void>(client.Poll()); return answers.size() == 2U; }, 8000), "cancelled requests did not finish");
        Require(answers[0].status == Status::Cancelled && answers[1].status == Status::Cancelled, "cancelled requests did not report Cancelled");
        Require(fixture.Logged("http GET /status/204") == before, "a request cancelled while it waited reached the server");
    }
    // The queue is bounded, and a refused request still reports through its callback.
    {
        ClientLimits limits;
        limits.workers = 1U;
        limits.maxQueued = 2U;
        Client client(limits);
        std::vector<Response> answers;
        Request slow = Get(fixture.Url(false, "/slow"));
        slow.timeoutMilliseconds = 20'000U;
        std::vector<RequestHandle> handles;
        for (int index = 0; index < 6; ++index) {
            handles.push_back(client.Send(slow, [&](Response&& response) { answers.push_back(std::move(response)); }));
        }
        client.Poll();
        std::size_t refused = 0U;
        for (const Response& answer : answers) {
            refused += answer.status == Status::Error && answer.error.find("too many") != std::string::npos ? 1U : 0U;
        }
        Require(refused >= 2U && client.Pending() <= 3U, "the request queue is not bounded");
        for (const RequestHandle& handle : handles) {
            handle.Cancel();
        }
        Require(WaitFor([&] { static_cast<void>(client.Poll()); return client.Pending() == 0U; }, 8000), "the cancelled queue did not drain");
    }
    // Destroying a client with a request in flight is prompt and does not call back.
    {
        const auto start = std::chrono::steady_clock::now();
        bool called = false;
        {
            Client client;
            Request slow = Get(fixture.Url(false, "/slow"));
            slow.timeoutMilliseconds = 20'000U;
            client.Send(slow, [&](Response&&) { called = true; });
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        Require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(3000), "destroying a client waited for the request");
        Require(!called, "a callback ran after its client was destroyed");
    }
    // An invalid request is reported through the callback too.
    {
        Client client;
        std::vector<Response> answers;
        Request bad;
        bad.url = "ftp://example.com/";
        client.Send(bad, [&](Response&& response) { answers.push_back(std::move(response)); });
        Require(WaitFor([&] { static_cast<void>(client.Poll()); return !answers.empty(); }, 2000) && answers.front().status == Status::InvalidRequest, "an invalid request was not reported");
    }
}

#endif // _WIN32

} // namespace

int main() {
    int failures = 0;
    const auto run = [&failures](const char* name, const std::function<void()>& test) {
        std::printf("START %s\n", name);
        std::fflush(stdout);
        try {
            test();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& error) {
            std::printf("FAIL %s: %s\n", name, error.what());
            ++failures;
        }
        std::fflush(stdout);
    };
#if defined(_WIN32)
    run("request-checks", [] { TestRequestChecks(); });
    Fixture fixture;
    std::string why;
    if (!StartFixture(fixture, why)) {
        std::printf("SKIP the server tests: %s\n", why.c_str());
        return failures != 0 ? 1 : kSkipped;
    }
    run("plain-requests", [&] { TestPlainRequests(fixture); });
    run("redirects", [&] { TestRedirects(fixture); });
    run("https", [&] { TestHttps(fixture); });
    run("timeouts-and-limits", [&] { TestTimeoutsAndLimits(fixture); });
    run("async-client", [&] { TestAsyncClient(fixture); });
    return failures == 0 ? 0 : 1;
#else
    // Other platforms have no backend: every request ends as Unsupported.
    run("unsupported", [] {
        kb::http::Request request;
        request.url = "https://example.com/";
        Require(kb::http::Perform(request).status == kb::http::Status::Unsupported, "a platform without a backend did not say so");
    });
    return failures == 0 ? 0 : 1;
#endif
}
