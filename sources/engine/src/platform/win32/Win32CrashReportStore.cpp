#include "engine/platform/CrashReporting.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace kb::platform {
namespace {

constexpr std::string_view kConsentFile = "upload-consent";
constexpr std::string_view kConsentGranted = "upload=yes\n";
constexpr std::uintmax_t kMaximumUploadBytes = 64U * 1024U * 1024U;
constexpr std::uintmax_t kMaximumMetadataBytes = 1024U * 1024U;
constexpr std::size_t kMaximumUploadsPerRun = 8U;

[[nodiscard]] bool IsReportFile(const std::filesystem::path& path) {
    const std::wstring name = path.filename().wstring();
    if (name.rfind(L"crash-", 0U) != 0U) {
        return false;
    }
    const std::wstring extension = path.extension().wstring();
    return extension == L".dmp" || extension == L".json";
}

[[nodiscard]] std::optional<std::string> ReadSmallFile(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > limit) {
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string bytes((std::istreambuf_iterator<char>{ stream }), std::istreambuf_iterator<char>{});
    return stream.bad() ? std::nullopt : std::optional<std::string>{ std::move(bytes) };
}

[[nodiscard]] std::wstring Wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), count));
    return result;
}

[[nodiscard]] std::string Trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1U);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1U);
    return std::string{ value };
}

// The string value of a top-level key in a description this module wrote. Its
// writer escapes only quotes, backslashes and control characters, so the
// reverse is all that is needed here.
[[nodiscard]] std::string JsonStringValue(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string{ key } + "\":\"";
    const std::size_t start = json.find(needle);
    if (start == std::string_view::npos) {
        return {};
    }
    std::string value;
    for (std::size_t index = start + needle.size(); index < json.size(); ++index) {
        const char character = json[index];
        if (character == '"') {
            return value;
        }
        if (character == '\\' && index + 1U < json.size()) {
            const char escaped = json[++index];
            value.push_back(escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped == 'r' ? '\r' : escaped);
            continue;
        }
        value.push_back(character);
    }
    return {};
}

struct ParsedEndpoint {
    bool secure = false;
    std::wstring host;
    INTERNET_PORT port = 0U;
    std::wstring path;
};

[[nodiscard]] bool IsLoopbackHost(std::wstring_view host) noexcept {
    return host == L"127.0.0.1" || host == L"::1" || host == L"[::1]";
}

[[nodiscard]] std::optional<ParsedEndpoint> ParseEndpoint(std::string_view url) {
    if (url.empty() || url.size() > 2048U ||
        std::ranges::any_of(url, [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte <= 0x20U || byte >= 0x7FU;
        })) {
        return std::nullopt;
    }
    const std::wstring wide = Wide(url);
    if (wide.empty()) {
        return std::nullopt;
    }
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);
    if (WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0U, &components) == FALSE ||
        components.dwHostNameLength == 0U || components.dwUserNameLength != 0U || components.dwPasswordLength != 0U) {
        return std::nullopt;
    }
    ParsedEndpoint endpoint;
    endpoint.host.assign(components.lpszHostName, components.dwHostNameLength);
    endpoint.port = components.nPort;
    endpoint.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength != 0U) {
        endpoint.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (endpoint.path.empty()) {
        endpoint.path = L"/";
    }
    if (components.nScheme == INTERNET_SCHEME_HTTPS) {
        endpoint.secure = true;
    } else if (components.nScheme != INTERNET_SCHEME_HTTP || !IsLoopbackHost(endpoint.host)) {
        // Reports leave the machine only encrypted. Plain HTTP stays on this machine.
        return std::nullopt;
    }
    return endpoint;
}

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() {
        if (value != nullptr) WinHttpCloseHandle(value);
    }
};

[[nodiscard]] std::string Boundary() {
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        std::memcpy(random.data(), &counter, sizeof(counter));
    }
    constexpr char kDigits[] = "0123456789abcdef";
    std::string boundary = "----21kbCrashReport";
    for (const unsigned char byte : random) {
        boundary.push_back(kDigits[byte >> 4U]);
        boundary.push_back(kDigits[byte & 0xFU]);
    }
    return boundary;
}

void AppendField(std::string& body, std::string_view boundary, std::string_view name, std::string_view value) {
    body += "--";
    body += boundary;
    body += "\r\nContent-Disposition: form-data; name=\"";
    body += name;
    body += "\"\r\n\r\n";
    body += value;
    body += "\r\n";
}

void AppendFile(std::string& body, std::string_view boundary, std::string_view name, std::string_view fileName,
    std::string_view contentType, std::string_view bytes) {
    body += "--";
    body += boundary;
    body += "\r\nContent-Disposition: form-data; name=\"";
    body += name;
    body += "\"; filename=\"";
    body += fileName;
    body += "\"\r\nContent-Type: ";
    body += contentType;
    body += "\r\n\r\n";
    body += bytes;
    body += "\r\n";
}

[[nodiscard]] std::string FieldText(std::string value) {
    // Form field values come from a description on disk; keep them to one
    // printable line so they cannot reshape the request.
    std::erase_if(value, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7FU;
    });
    if (value.size() > 256U) {
        value.resize(256U);
    }
    return value;
}

[[nodiscard]] bool Post(const ParsedEndpoint& endpoint, const std::string& contentType, const std::string& body,
    std::string& error) {
    InternetHandle session{ WinHttpOpen(L"21kb-crash-reporter/1",
        endpoint.secure ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0U) };
    if (session.value == nullptr) {
        error = "WinHttpOpen failed with " + std::to_string(GetLastError());
        return false;
    }
    static_cast<void>(WinHttpSetTimeouts(session.value, 10'000, 10'000, 30'000, 30'000));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#if defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3)
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (endpoint.secure &&
        WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)) == FALSE) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        static_cast<void>(WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)));
    }
    InternetHandle connection{ WinHttpConnect(session.value, endpoint.host.c_str(), endpoint.port, 0U) };
    if (connection.value == nullptr) {
        error = "WinHttpConnect failed with " + std::to_string(GetLastError());
        return false;
    }
    InternetHandle request{ WinHttpOpenRequest(connection.value, L"POST", endpoint.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, endpoint.secure ? WINHTTP_FLAG_SECURE : 0U) };
    if (request.value == nullptr) {
        error = "WinHttpOpenRequest failed with " + std::to_string(GetLastError());
        return false;
    }
    // A redirect could move the report to a host nobody configured.
    DWORD disable = WINHTTP_DISABLE_REDIRECTS;
    static_cast<void>(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)));
    const std::wstring headers = L"Content-Type: " + Wide(contentType) + L"\r\n";
    if (WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0U) == FALSE ||
        WinHttpReceiveResponse(request.value, nullptr) == FALSE) {
        error = "crash report request failed with " + std::to_string(GetLastError());
        return false;
    }
    DWORD status = 0U;
    DWORD statusBytes = sizeof(status);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX) == FALSE) {
        error = "crash report response had no status";
        return false;
    }
    if (status < 200U || status >= 300U) {
        error = "crash report endpoint answered " + std::to_string(status);
        return false;
    }
    return true;
}

} // namespace

std::string ReadCrashReportPrivacyNotice(const std::filesystem::path& directory) {
    return ReadSmallFile(directory / kCrashReportPrivacyNoticeFile, 64U * 1024U).value_or(std::string{});
}

std::string ReadCrashReportPrivacyNotice() {
    std::vector<wchar_t> path(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0U || length >= path.size()) {
        return {};
    }
    return ReadCrashReportPrivacyNotice(std::filesystem::path{ std::wstring_view{ path.data(), length } }.parent_path());
}

std::vector<CrashReportFiles> ListCrashReports(const std::filesystem::path& directory) {
    std::vector<CrashReportFiles> reports;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end; !error && iterator != end;
         iterator.increment(error)) {
        const std::filesystem::path& path = iterator->path();
        if (path.extension() != L".dmp" || !IsReportFile(path) || !iterator->is_regular_file(error)) {
            continue;
        }
        std::filesystem::path metadata = path;
        metadata.replace_extension(L".json");
        if (std::filesystem::is_regular_file(metadata, error)) {
            reports.push_back(CrashReportFiles{ path, std::move(metadata) });
        }
    }
    std::ranges::sort(reports, {}, &CrashReportFiles::minidump);
    return reports;
}

std::size_t DeleteCrashReports(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end; !error && iterator != end;
         iterator.increment(error)) {
        if (IsReportFile(iterator->path()) && iterator->is_regular_file(error)) {
            files.push_back(iterator->path());
        }
    }
    std::size_t removed = 0U;
    for (const std::filesystem::path& file : files) {
        if (std::filesystem::remove(file, error)) {
            ++removed;
        }
    }
    return removed;
}

bool HasCrashUploadConsent(const std::filesystem::path& directory) {
    const std::optional<std::string> value = ReadSmallFile(directory / kConsentFile, 64U);
    return value.has_value() && *value == kConsentGranted;
}

bool SetCrashUploadConsent(const std::filesystem::path& directory, bool consent) {
    std::error_code error;
    const std::filesystem::path path = directory / kConsentFile;
    if (!consent) {
        std::filesystem::remove(path, error);
        return !error && !std::filesystem::exists(path, error);
    }
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << kConsentGranted;
    stream.close();
    return !stream.fail();
}

bool IsAllowedCrashUploadEndpoint(std::string_view url) noexcept {
    try {
        return ParseEndpoint(url).has_value();
    } catch (...) {
        return false;
    }
}

std::string ReadCrashUploadEndpoint(const std::filesystem::path& configFile) {
    const std::optional<std::string> text = ReadSmallFile(configFile, 64U * 1024U);
    if (!text.has_value()) {
        return {};
    }
    std::string_view remaining{ *text };
    bool inSection = false;
    while (!remaining.empty()) {
        const std::size_t newline = remaining.find('\n');
        const std::string line = Trim(remaining.substr(0U, newline));
        remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1U);
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            inSection = line == "[CrashReports]";
            continue;
        }
        const std::size_t equals = line.find('=');
        if (inSection && equals != std::string::npos && Trim(std::string_view{ line }.substr(0U, equals)) == "UploadUrl") {
            std::string url = Trim(std::string_view{ line }.substr(equals + 1U));
            return IsAllowedCrashUploadEndpoint(url) ? url : std::string{};
        }
    }
    return {};
}

CrashUploadSummary UploadPendingCrashReports(const std::filesystem::path& directory, std::string_view endpoint) {
    CrashUploadSummary summary;
    const std::optional<ParsedEndpoint> parsed = ParseEndpoint(endpoint);
    if (!parsed.has_value()) {
        summary.error = "crash report endpoint is not allowed";
        return summary;
    }
    if (!HasCrashUploadConsent(directory)) {
        summary.error = "crash report upload has no consent";
        return summary;
    }
    std::vector<CrashReportFiles> reports = ListCrashReports(directory);
    if (reports.size() > kMaximumUploadsPerRun) {
        reports.resize(kMaximumUploadsPerRun);
    }
    for (const CrashReportFiles& report : reports) {
        summary.attempted = true;
        const std::optional<std::string> dump = ReadSmallFile(report.minidump, kMaximumUploadBytes);
        const std::optional<std::string> metadata = ReadSmallFile(report.metadata, kMaximumMetadataBytes);
        if (!dump.has_value() || !metadata.has_value() || dump->size() < 4U || dump->compare(0U, 4U, "MDMP") != 0) {
            ++summary.failed;
            summary.error = "crash report could not be read";
            continue;
        }
        std::string boundary = Boundary();
        while (dump->find(boundary) != std::string::npos || metadata->find(boundary) != std::string::npos) {
            boundary = Boundary();
        }
        std::string body;
        body.reserve(dump->size() + metadata->size() + 1024U);
        AppendField(body, boundary, "prod", FieldText(JsonStringValue(*metadata, "product")));
        AppendField(body, boundary, "ver", FieldText(JsonStringValue(*metadata, "version")));
        AppendField(body, boundary, "build_id", FieldText(JsonStringValue(*metadata, "buildId")));
        AppendFile(body, boundary, "metadata", "metadata.json", "application/json", *metadata);
        AppendFile(body, boundary, "upload_file_minidump", report.minidump.filename().string(),
            "application/octet-stream", *dump);
        body += "--";
        body += boundary;
        body += "--\r\n";
        std::string error;
        if (!Post(*parsed, "multipart/form-data; boundary=" + boundary, body, error)) {
            ++summary.failed;
            summary.error = std::move(error);
            continue;
        }
        // Sent and accepted: the local copy has done its job.
        std::error_code removeError;
        std::filesystem::remove(report.minidump, removeError);
        std::filesystem::remove(report.metadata, removeError);
        ++summary.uploaded;
    }
    return summary;
}

} // namespace kb::platform
