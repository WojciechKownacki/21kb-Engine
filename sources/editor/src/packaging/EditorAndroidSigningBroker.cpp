#include "packaging/EditorAndroidSigningBroker.hpp"

#include "engine/core/JsonValue.hpp"
#include "packaging/EditorPackageInputValidation.hpp"
#include "packaging/EditorPackageProcessEnvironment.hpp"
#include "packaging/EditorSigningProcess.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>
#include <utility>

namespace kb::editor {
namespace {

using signing_process::Canonical;
using signing_process::DirectChildOf;
using signing_process::SamePath;
using signing_process::ValidSession;

constexpr std::size_t kMaximumOutputBytes = 1024U * 1024U;

struct SigningRequest {
    std::string session;
    std::filesystem::path java;
    std::filesystem::path apksignerJar;
    std::filesystem::path keystore;
    std::string keyAlias;
    std::filesystem::path inputApk;
    std::filesystem::path outputApk;
};

[[nodiscard]] const std::string* StringMember(const kb::core::JsonValue& object, std::string_view name) noexcept {
    const kb::core::JsonValue* value = object.Find(name);
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String ? &value->AsString() : nullptr;
}

[[nodiscard]] std::optional<SigningRequest> ReadRequest(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { error = "Signing request could not be opened."; return std::nullopt; }
    std::string bytes((std::istreambuf_iterator<char>{ stream }), std::istreambuf_iterator<char>{});
    if (bytes.size() > 64U * 1024U) { error = "Signing request exceeds the size limit."; return std::nullopt; }
    kb::core::JsonValue json;
    if (!kb::core::JsonValue::Parse(bytes, json, error) || json.GetKind() != kb::core::JsonValue::Kind::Object || json.Size() != 8U) {
        if (error.empty()) error = "Signing request schema is invalid.";
        return std::nullopt;
    }
    const kb::core::JsonValue* schema = json.Find("schema");
    const std::string* session = StringMember(json, "session");
    const std::string* java = StringMember(json, "java");
    const std::string* jar = StringMember(json, "apksignerJar");
    const std::string* keystore = StringMember(json, "keystore");
    const std::string* alias = StringMember(json, "keyAlias");
    const std::string* input = StringMember(json, "inputApk");
    const std::string* output = StringMember(json, "outputApk");
    if (schema == nullptr || schema->GetKind() != kb::core::JsonValue::Kind::Number || schema->AsNumber() != 1.0 ||
        session == nullptr || java == nullptr || jar == nullptr || keystore == nullptr || alias == nullptr || input == nullptr || output == nullptr) {
        error = "Signing request schema is invalid.";
        return std::nullopt;
    }
    return SigningRequest{ *session, *java, *jar, *keystore, *alias, *input, *output };
}

[[nodiscard]] bool JarAllowed(const std::filesystem::path& jar) {
    const auto canonical = Canonical(jar);
    if (canonical.empty() || canonical.filename() != "apksigner.jar" || canonical.parent_path().filename() != "lib" ||
        canonical.parent_path().parent_path().parent_path().filename() != "build-tools") return false;
    std::vector<std::filesystem::path> roots;
#if defined(_WIN32)
    for (const wchar_t* name : { L"ANDROID_SDK_ROOT", L"ANDROID_HOME" }) {
        std::array<wchar_t, 32768> value{};
        const DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
        if (length > 0U && length < value.size()) roots.emplace_back(std::wstring_view{ value.data(), length });
    }
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0U, nullptr, &local)) && local != nullptr) {
        roots.emplace_back(std::filesystem::path{ local } / "Android" / "Sdk");
        CoTaskMemFree(local);
    }
#endif
    return std::ranges::any_of(roots, [&](const std::filesystem::path& root) {
        return SamePath(canonical.parent_path().parent_path().parent_path(), Canonical(root / "build-tools"));
    });
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::wstring> EnvironmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0U);
    if (required == 0U) return std::nullopt;
    std::vector<wchar_t> value(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), required);
    if (length == 0U || length >= required) return std::nullopt;
    return std::wstring{ value.data(), length };
}

[[nodiscard]] std::vector<std::filesystem::path> TrustedJavaExecutables() {
    std::vector<std::filesystem::path> candidates;
    const auto add = [&](const std::filesystem::path& candidate) {
        const std::filesystem::path canonical = Canonical(candidate);
        if (!canonical.empty() && std::filesystem::is_regular_file(canonical) &&
            std::ranges::none_of(candidates, [&](const std::filesystem::path& existing) {
                return SamePath(existing, canonical);
            })) {
            candidates.push_back(canonical);
        }
    };
    if (const std::optional<std::wstring> javaHome = EnvironmentValue(L"JAVA_HOME"); javaHome.has_value()) {
        add(std::filesystem::path{ *javaHome } / "bin" / "java.exe");
    }
    if (const std::optional<std::wstring> path = EnvironmentValue(L"PATH"); path.has_value()) {
        std::wstring_view remaining{ *path };
        while (true) {
            const std::size_t separator = remaining.find(L';');
            std::wstring_view component = remaining.substr(0U, separator);
            if (component.size() >= 2U && component.front() == L'\"' && component.back() == L'\"') {
                component = component.substr(1U, component.size() - 2U);
            }
            if (!component.empty()) add(std::filesystem::path{ component } / "java.exe");
            if (separator == std::wstring_view::npos) break;
            remaining.remove_prefix(separator + 1U);
        }
    }
    return candidates;
}
#endif

#if defined(_WIN32)
using signing_process::Guard;
using signing_process::ScopedHandle;

[[nodiscard]] bool WriteResponse(const std::filesystem::path& path, std::string_view session) {
    kb::core::JsonValue response = kb::core::JsonValue::MakeObject();
    response.Set("schema", kb::core::JsonValue::MakeNumber(1.0));
    response.Set("session", kb::core::JsonValue::MakeString(std::string{ session }));
    response.Set("succeeded", kb::core::JsonValue::MakeBool(true));
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

bool EditorAndroidSigningBroker::IsTrustedJavaExecutable(const std::filesystem::path& path) {
#if defined(_WIN32)
    if (!path.is_absolute()) return false;
    const std::filesystem::path canonical = Canonical(path);
    if (canonical.empty()) return false;
    return std::ranges::any_of(TrustedJavaExecutables(), [&](const std::filesystem::path& candidate) {
        return SamePath(canonical, candidate);
    });
#else
    static_cast<void>(path);
    return false;
#endif
}

void EditorAndroidSigningBroker::SecureClear(std::string& value) noexcept {
    signing_process::SecureClear(value);
}

EditorAndroidSigningResult EditorAndroidSigningBroker::Execute(
    const std::filesystem::path& requestFile, const std::filesystem::path& responseFile,
    const std::filesystem::path& expectedJobsRoot,
    const std::filesystem::path& expectedKeystore, const std::string& expectedAlias,
    std::string& storePassword, std::string& keyPassword, void* processJob) {
    EditorAndroidSigningResult result;
    const auto clearSecrets = [&]() { SecureClear(storePassword); SecureClear(keyPassword); };
#if !defined(_WIN32)
    clearSecrets();
    result.message = "Android signing is supported only by the Windows editor.";
    return result;
#else
    std::string error;
    const std::optional<SigningRequest> request = ReadRequest(requestFile, error);
    const std::filesystem::path jobRoot = Canonical(requestFile).parent_path();
    const std::filesystem::path jobsRoot = Canonical(expectedJobsRoot);
    const std::filesystem::path trustedJava = request ? Canonical(request->java) : std::filesystem::path{};
    std::error_code filesystemError;
    const bool outputExists = std::filesystem::exists(request ? request->outputApk : std::filesystem::path{}, filesystemError);
    filesystemError.clear();
    const bool responseExists = std::filesystem::exists(responseFile, filesystemError);
    if (!request || !ValidSession(request->session) ||
        !package_input::IsValidAndroidKeyAlias(request->keyAlias) || request->keyAlias != expectedAlias ||
        !requestFile.is_absolute() || !responseFile.is_absolute() || !request->java.is_absolute() ||
        !request->apksignerJar.is_absolute() || !request->keystore.is_absolute() ||
        !request->inputApk.is_absolute() || !request->outputApk.is_absolute() ||
        jobsRoot.empty() || !DirectChildOf(jobsRoot, jobRoot) ||
        !DirectChildOf(jobRoot, requestFile) ||
        !DirectChildOf(jobRoot, responseFile) || !DirectChildOf(jobRoot, request->inputApk) ||
        !DirectChildOf(jobRoot, request->outputApk) || SamePath(request->inputApk, request->outputApk) ||
        outputExists || responseExists || !std::filesystem::is_regular_file(request->inputApk) ||
        !IsTrustedJavaExecutable(request->java) || !JarAllowed(request->apksignerJar) ||
        !SamePath(request->keystore, expectedKeystore) || !std::filesystem::is_regular_file(expectedKeystore) ||
        storePassword.empty() || keyPassword.empty()) {
        clearSecrets();
        result.message = error.empty() ? "Android signing request violates the build allowlist." : error;
        return result;
    }
    ScopedHandle jobsGuard = Guard(jobsRoot, true, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE);
    ScopedHandle rootGuard = Guard(jobRoot, true, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE);
    ScopedHandle inputGuard = Guard(request->inputApk, false, GENERIC_READ, FILE_SHARE_READ);
    ScopedHandle javaGuard = Guard(trustedJava, false, GENERIC_READ, FILE_SHARE_READ);
    ScopedHandle jarGuard = Guard(request->apksignerJar, false, GENERIC_READ, FILE_SHARE_READ);
    ScopedHandle keystoreGuard = Guard(expectedKeystore, false, GENERIC_READ, FILE_SHARE_READ);
    if (!jobsGuard || !rootGuard || !inputGuard || !javaGuard || !jarGuard || !keystoreGuard) {
        clearSecrets();
        result.message = "Android signing inputs could not be guarded against path replacement.";
        return result;
    }

    std::vector<std::wstring> arguments{ L"-classpath", request->apksignerJar.wstring(), L"com.android.apksigner.ApkSignerTool",
        L"sign", L"--pass-encoding", L"utf-8", L"--debuggable-apk-permitted", L"false", L"--alignment-preserved", L"true",
        L"--v4-signing-enabled", L"false", L"--ks", expectedKeystore.wstring(), L"--ks-key-alias", std::wstring{ expectedAlias.begin(), expectedAlias.end() },
        L"--ks-pass", L"stdin", L"--key-pass", L"stdin", L"--out", request->outputApk.wstring(), request->inputApk.wstring() };
    signing_process::SignerRun run = signing_process::RunSigner(trustedJava, arguments, jobRoot,
        { &storePassword, &keyPassword }, processJob, std::chrono::seconds{ 180 }, kMaximumOutputBytes);
    clearSecrets();
    result.toolOutput = std::move(run.output);
    if (!run.started) {
        result.message = run.error.empty() ? "ApkSignerTool could not be started." : run.error;
        return result;
    }
    result.succeeded = run.secretsWritten && !run.timedOut && run.exitCode == 0U && std::filesystem::is_regular_file(request->outputApk) &&
        WriteResponse(responseFile, request->session);
    result.message = result.succeeded ? "Android release package signed." : "ApkSignerTool rejected or did not publish the signed package.";
    if (!result.succeeded) std::filesystem::remove(request->outputApk, filesystemError);
    return result;
#endif
}

} // namespace kb::editor
