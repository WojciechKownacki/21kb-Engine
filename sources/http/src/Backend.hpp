#pragma once

#include "kb/http/HttpClient.hpp"

#include <string>
#include <vector>

// What a platform backend provides: one request, one hop, no redirects. Redirects, limits on the request
// and the common checks are in HttpClient.cpp.
namespace kb::http::detail {

struct ParsedUrl {
    bool https = false;
    std::string host;
    std::uint16_t port = 80U;
    std::string target = "/"; // path and query
    std::string text;         // the URL without a fragment
};

[[nodiscard]] bool ParseUrl(const std::string& url, ParsedUrl& parsed, std::string& error);

struct HopRequest {
    std::string method;
    ParsedUrl url;
    std::vector<Header> headers;
    const std::vector<std::uint8_t>* body = nullptr;
    std::uint32_t timeoutMilliseconds = 0U; // what is left of the whole request
    std::size_t maxResponseBytes = 0U;
    std::string pinnedCertificateSha256;
};

struct HopResponse {
    Status status = Status::Error;
    int httpStatus = 0;
    std::vector<Header> headers;
    std::vector<std::uint8_t> body;
    std::string error;
};

[[nodiscard]] HopResponse PerformHop(const HopRequest& request, CancelToken& token);

} // namespace kb::http::detail
