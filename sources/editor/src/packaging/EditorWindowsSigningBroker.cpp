#include "packaging/EditorWindowsSigningBroker.hpp"

#include "engine/core/JsonValue.hpp"
#include "packaging/EditorSigningProcess.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::editor {
namespace {

using signing_process::Canonical;
using signing_process::DirectChildOf;
using signing_process::SamePath;
using signing_process::ValidSession;

constexpr std::size_t kMaximumOutputBytes = 1024U * 1024U;
constexpr std::size_t kMaximumImages = 1024U;

struct SigningRequest {
    std::string session;
    std::filesystem::path certificate;
    std::string timestampUrl;
    std::vector<std::filesystem::path> images;
};

[[nodiscard]] const std::string* StringMember(const kb::core::JsonValue& object, std::string_view name) noexcept {
    const kb::core::JsonValue* value = object.Find(name);
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String ? &value->AsString() : nullptr;
}

[[nodiscard]] std::filesystem::path Utf8Path(const std::string& text) {
    return std::filesystem::path{ std::u8string{ text.begin(), text.end() } };
}

[[nodiscard]] std::optional<SigningRequest> ReadRequest(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { error = "Signing request could not be opened."; return std::nullopt; }
    std::string bytes((std::istreambuf_iterator<char>{ stream }), std::istreambuf_iterator<char>{});
    if (bytes.size() > 1024U * 1024U) { error = "Signing request exceeds the size limit."; return std::nullopt; }
    kb::core::JsonValue json;
    if (!kb::core::JsonValue::Parse(bytes, json, error) || json.GetKind() != kb::core::JsonValue::Kind::Object || json.Size() != 6U) {
        if (error.empty()) error = "Signing request schema is invalid.";
        return std::nullopt;
    }
    const kb::core::JsonValue* schema = json.Find("schema");
    const std::string* kind = StringMember(json, "kind");
    const std::string* session = StringMember(json, "session");
    const std::string* certificate = StringMember(json, "certificate");
    const std::string* timestampUrl = StringMember(json, "timestampUrl");
    const kb::core::JsonValue* files = json.Find("files");
    if (schema == nullptr || schema->GetKind() != kb::core::JsonValue::Kind::Number || schema->AsNumber() != 1.0 ||
        kind == nullptr || *kind != "windows-authenticode" || session == nullptr || certificate == nullptr ||
        timestampUrl == nullptr || files == nullptr || files->GetKind() != kb::core::JsonValue::Kind::Array ||
        files->Size() == 0U || files->Size() > kMaximumImages) {
        error = "Signing request schema is invalid.";
        return std::nullopt;
    }
    SigningRequest request{ *session, Utf8Path(*certificate), *timestampUrl, {} };
    for (std::size_t index = 0U; index < files->Size(); ++index) {
        const kb::core::JsonValue* file = files->At(index);
        if (file == nullptr || file->GetKind() != kb::core::JsonValue::Kind::String) {
            error = "Signing request schema is invalid.";
            return std::nullopt;
        }
        request.images.push_back(Utf8Path(file->AsString()));
    }
    return request;
}

[[nodiscard]] bool IsImage(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return extension == L".exe" || extension == L".dll";
}

[[nodiscard]] std::string SignerThumbprint(std::string_view output) {
    // SIGNER|<40 hex digits>|<subject>
    const std::size_t marker = output.rfind("SIGNER|");
    if (marker == std::string_view::npos || (marker != 0U && output[marker - 1U] != '\n')) return {};
    const std::string_view thumbprint = output.substr(marker + 7U, 40U);
    if (thumbprint.size() != 40U || output.size() <= marker + 47U || output[marker + 47U] != '|' ||
        !std::ranges::all_of(thumbprint, [](char character) {
            return std::isdigit(static_cast<unsigned char>(character)) != 0 || (character >= 'A' && character <= 'F');
        })) {
        return {};
    }
    return std::string{ thumbprint };
}

#if defined(_WIN32)
[[nodiscard]] bool WriteResponse(const std::filesystem::path& path, std::string_view session, std::string_view thumbprint) {
    kb::core::JsonValue response = kb::core::JsonValue::MakeObject();
    response.Set("schema", kb::core::JsonValue::MakeNumber(1.0));
    response.Set("session", kb::core::JsonValue::MakeString(std::string{ session }));
    response.Set("succeeded", kb::core::JsonValue::MakeBool(true));
    response.Set("signerThumbprint", kb::core::JsonValue::MakeString(std::string{ thumbprint }));
    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << response.Dump();
    stream.close();
    if (!stream) return false;
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}
#endif

} // namespace

std::filesystem::path EditorWindowsSigningBroker::DefaultSigner() {
#if defined(_WIN32)
    std::vector<wchar_t> path(32768U, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0U || length >= path.size()) return {};
    return std::filesystem::path{ std::wstring_view{ path.data(), length } }.parent_path() / L"kb_authenticode_signer.exe";
#else
    return {};
#endif
}

EditorWindowsSigningResult EditorWindowsSigningBroker::Execute(
    const std::filesystem::path& requestFile, const std::filesystem::path& responseFile,
    const std::filesystem::path& expectedJobsRoot, const std::filesystem::path& expectedCertificate,
    const std::string& expectedTimestampUrl, const std::filesystem::path& signer,
    std::string& password, void* processJob) {
    EditorWindowsSigningResult result;
#if !defined(_WIN32)
    signing_process::SecureClear(password);
    result.message = "Windows signing is supported only by the Windows editor.";
    static_cast<void>(requestFile); static_cast<void>(responseFile); static_cast<void>(expectedJobsRoot);
    static_cast<void>(expectedCertificate); static_cast<void>(expectedTimestampUrl); static_cast<void>(signer);
    static_cast<void>(processJob);
    return result;
#else
    std::string error;
    const std::optional<SigningRequest> request = ReadRequest(requestFile, error);
    const std::filesystem::path jobRoot = Canonical(requestFile).parent_path();
    const std::filesystem::path jobsRoot = Canonical(expectedJobsRoot);
    const std::filesystem::path imageRoot = jobRoot / L"authenticode";
    std::error_code filesystemError;
    const bool responseExists = std::filesystem::exists(responseFile, filesystemError);
    bool imagesValid = request.has_value();
    if (request.has_value()) {
        for (std::size_t index = 0U; index < request->images.size() && imagesValid; ++index) {
            const std::filesystem::path& image = request->images[index];
            imagesValid = image.is_absolute() && IsImage(image) && DirectChildOf(imageRoot, image) &&
                std::filesystem::is_regular_file(image, filesystemError) &&
                std::none_of(request->images.begin(), request->images.begin() + static_cast<std::ptrdiff_t>(index),
                    [&](const std::filesystem::path& earlier) { return SamePath(earlier, image); });
        }
    }
    if (!request || !imagesValid || !ValidSession(request->session) ||
        !requestFile.is_absolute() || !responseFile.is_absolute() || !signer.is_absolute() ||
        !request->certificate.is_absolute() || jobsRoot.empty() || !DirectChildOf(jobsRoot, jobRoot) ||
        !DirectChildOf(jobRoot, requestFile) || !DirectChildOf(jobRoot, responseFile) || !DirectChildOf(jobRoot, imageRoot) ||
        responseExists || !SamePath(request->certificate, expectedCertificate) ||
        !std::filesystem::is_regular_file(expectedCertificate, filesystemError) ||
        request->timestampUrl != expectedTimestampUrl || !std::filesystem::is_regular_file(signer, filesystemError)) {
        signing_process::SecureClear(password);
        result.message = error.empty() ? "Windows signing request violates the build allowlist." : error;
        return result;
    }
    using signing_process::Guard;
    using signing_process::ScopedHandle;
    std::vector<ScopedHandle> guards;
    guards.push_back(Guard(jobsRoot, true, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE));
    guards.push_back(Guard(jobRoot, true, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE));
    guards.push_back(Guard(imageRoot, true, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE));
    guards.push_back(Guard(expectedCertificate, false, GENERIC_READ, FILE_SHARE_READ));
    guards.push_back(Guard(signer, false, GENERIC_READ, FILE_SHARE_READ));
    for (const std::filesystem::path& image : request->images) {
        // Shared for writing: the signer rewrites the image in place.
        guards.push_back(Guard(image, false, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE));
    }
    if (std::ranges::any_of(guards, [](const ScopedHandle& guard) { return !guard; })) {
        signing_process::SecureClear(password);
        result.message = "Windows signing inputs could not be guarded against path replacement.";
        return result;
    }

    std::vector<std::wstring> arguments{ L"--pfx", expectedCertificate.wstring() };
    if (!expectedTimestampUrl.empty()) {
        arguments.emplace_back(L"--timestamp-url");
        arguments.emplace_back(expectedTimestampUrl.begin(), expectedTimestampUrl.end());
    }
    arguments.emplace_back(L"--");
    for (const std::filesystem::path& image : request->images) arguments.push_back(image.wstring());
    // Timestamping talks to a remote service per image, so the budget scales with the count.
    const auto timeout = std::chrono::seconds{ 120 + 30 * static_cast<long long>(request->images.size()) };
    signing_process::SignerRun run = signing_process::RunSigner(
        signer, arguments, jobRoot, { &password }, processJob, timeout, kMaximumOutputBytes);
    signing_process::SecureClear(password);
    result.toolOutput = std::move(run.output);
    if (!run.started) {
        result.message = run.error.empty() ? "The Authenticode signer could not be started." : run.error;
        return result;
    }
    result.signerThumbprint = SignerThumbprint(result.toolOutput);
    result.succeeded = run.secretsWritten && !run.timedOut && run.exitCode == 0U && !result.signerThumbprint.empty() &&
        WriteResponse(responseFile, request->session, result.signerThumbprint);
    result.message = result.succeeded ? "Windows images signed." : "The Authenticode signer rejected the request or failed.";
    return result;
#endif
}

} // namespace kb::editor
