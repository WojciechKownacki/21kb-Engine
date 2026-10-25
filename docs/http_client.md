# HTTP client for gameplay (kb_http)

`kb_http` (`sources/http`, header `kb/http/HttpClient.hpp`) is a small HTTP and HTTPS client for web APIs,
leaderboards and account services. It does not depend on the engine and nothing in the engine links it; a
game that wants it links `kb_http`.

```cpp
kb::http::Client client;                       // two worker threads, a bounded queue
kb::http::Request request;
request.url = "https://api.example.com/scores";
request.headers.push_back({ "Authorization", "Bearer ..." });
client.Send(request, [](kb::http::Response&& answer) {
    if (answer.status == kb::http::Status::Ok && answer.httpStatus == 200) { /* answer.body */ }
});
// once per frame, on the game thread:
client.Poll();                                 // runs the callbacks of finished requests here
```

`Client::Send` returns a `RequestHandle`; `Cancel()` is safe from any thread. `Perform(request)` does one
request on the calling thread, for tools and tests.

## What it enforces

* **Schemes.** Only `http://` and `https://`. `http://` is refused unless `allowInsecureHttp` is set, and a
  redirect from `https://` to `http://` is always refused (`Status::InsecureBlocked`). A URL with a user name
  or password, a space or control character, an IPv6 literal, or a bad port or host is invalid.
* **Headers.** Names are tokens; values cannot contain line breaks; `Host`, `Content-Length`,
  `Transfer-Encoding`, `Connection`, `Upgrade`, `Expect`, `TE` and `Trailer` belong to the library.
  A body is allowed only with POST, PUT and PATCH (at most 16 MiB).
* **Time.** `timeoutMilliseconds` bounds the whole request, redirects included. WinHTTP's own timeouts are
  per operation and retry silently, so a watchdog ends the call at the deadline; a server that sends one byte at
  a time cannot hold a request past it.
* **Size.** `maxResponseBytes` (default 1 MiB, at most 256 MiB) is checked against `Content-Length` and
  while reading, so a chunked answer without end is cut off (`Status::TooLarge`).
* **Redirects.** At most `maxRedirects` (default 5), followed one checked hop at a time: 303, and 301 or 302
  after a POST, become GET without the body; 307 and 308 keep method and body. A redirect to another host,
  port or scheme drops `Authorization`, `Cookie` and `Proxy-Authorization`. A redirect to a scheme other than
  http(s) is refused.
* **TLS.** TLS 1.2 and 1.3 only. The operating system checks the chain and the host name
  (`Status::CertificateError` with the reason). For a service with its own certificate, set
  `pinnedCertificateSha256` to the SHA-256 of that certificate: the connection is accepted only if the server
  presents exactly that certificate, the chain and name checks are then not used, and a mismatch is found
  before any part of the request is sent. Pinned requests use a separate WinHTTP session, so their relaxed
  checks can never apply to another request. Revocation is not checked.
* **Queue.** At most `maxQueued` requests wait for a worker; beyond that, and for an invalid request, the
  callback still runs (from `Poll`) with the reason. Destroying the client cancels what runs and drops what waits,
  without calling back.

## Not covered

* Platforms other than Windows: every request ends as `Status::Unsupported` (no TLS library is bundled and none
  is added without an entry in the license manifest). Linux and Android would need libcurl or the platform's
  own API, macOS and iOS NSURLSession, the browser `fetch`; none is written.
* Cookies, caching, HTTP/2 specifics, compression, proxies with credentials, WebSocket, client certificates,
  streaming a large body to disk.

## Tests

`kb_http_tests` runs the checks that need no network, then starts `tests/http_test_server.py` (Python) with a
throwaway self-signed certificate made by `openssl` (the one in Git for Windows), and tests plain requests and
all methods, redirects (302, 303, 307, relative, loop, to a file URL, across hosts, https to http), HTTPS with a
pinned certificate, a wrong pin (the request is never sent), an untrusted certificate, timeouts including a
dripping server, size limits including an endless chunked answer, the async client (callbacks on the polling
thread, cancellation while running and while queued, a bounded queue, destruction in flight). Without Python or
openssl the server part is reported as skipped, not passed.
