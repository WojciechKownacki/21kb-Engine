#include "packaging/EditorAndroidSigningBroker.hpp"

#include "engine/core/JsonValue.hpp"
#include "packaging/EditorPackageInputValidation.hpp"
#include "packaging/EditorPackageProcessEnvironment.hpp"

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

[[nodiscard]] std::filesystem::path Canonical(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
    return error ? std::filesystem::path{} : result;
}

[[nodiscard]] bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept {
    const auto lhs = Canonical(left);
    const auto rhs = Canonical(right);
    if (lhs.empty() || rhs.empty()) return false;
#if defined(_WIN32)
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
#else
    return lhs == rhs;
#endif
}

[[nodiscard]] bool DirectChildOf(const std::filesystem::path& root, const std::filesystem::path& child) noexcept {
    return !Canonical(root).empty() && SamePath(Canonical(child).parent_path(), root);
}

[[nodiscard]] bool ValidSession(std::string_view value) noexcept {
    return value.size() == 32U && std::ranges::all_of(value, [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0 || (character >= 'a' && character <= 'f');
    });
}

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
class ScopedHandle final {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE value) noexcept : value_(value) {}
    ~ScopedHandle() { if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE Get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_ = nullptr;
};

[[nodiscard]] ScopedHandle Guard(const std::filesystem::path& path, bool directory, DWORD access, DWORD share) noexcept {
    ScopedHandle handle{ CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U), nullptr) };
    if (!handle) return {};
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(handle.Get(), FileAttributeTagInfo, &info, sizeof(info)) ||
        (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        (((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) != directory)) return {};
    return handle;
}

[[nodiscard]] std::wstring Quote(std::wstring_view argument) {
    std::wstring result{ L'\"' };
    std::size_t slashes = 0U;
    for (wchar_t character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') result.append((slashes * 2U) + 1U, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0U;
        result.push_back(character);
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] bool WriteSecret(HANDLE pipe, std::string_view secret) noexcept {
    DWORD written = 0U;
    if (!WriteFile(pipe, secret.data(), static_cast<DWORD>(secret.size()), &written, nullptr) || written != secret.size()) return false;
    constexpr char newline = '\n';
    return WriteFile(pipe, &newline, 1U, &written, nullptr) && written == 1U;
}

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
    if (value.capacity() > value.size()) value.resize(value.capacity(), '\0');
    volatile char* bytes = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0U; index < value.size(); ++index) bytes[index] = '\0';
    value.clear();
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

    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE inputReadRaw = nullptr, inputWriteRaw = nullptr, outputReadRaw = nullptr, outputWriteRaw = nullptr;
    if (!CreatePipe(&inputReadRaw, &inputWriteRaw, &security, 0U) || !CreatePipe(&outputReadRaw, &outputWriteRaw, &security, 0U) ||
        !SetHandleInformation(inputWriteRaw, HANDLE_FLAG_INHERIT, 0U) || !SetHandleInformation(outputReadRaw, HANDLE_FLAG_INHERIT, 0U)) {
        if (inputReadRaw) CloseHandle(inputReadRaw); if (inputWriteRaw) CloseHandle(inputWriteRaw);
        if (outputReadRaw) CloseHandle(outputReadRaw); if (outputWriteRaw) CloseHandle(outputWriteRaw);
        clearSecrets(); result.message = "Secure signer pipes could not be created."; return result;
    }
    ScopedHandle inputRead{ inputReadRaw }, inputWrite{ inputWriteRaw }, outputRead{ outputReadRaw }, outputWrite{ outputWriteRaw };
    std::vector<std::wstring> arguments{ L"-classpath", request->apksignerJar.wstring(), L"com.android.apksigner.ApkSignerTool",
        L"sign", L"--pass-encoding", L"utf-8", L"--debuggable-apk-permitted", L"false", L"--alignment-preserved", L"true",
        L"--v4-signing-enabled", L"false", L"--ks", expectedKeystore.wstring(), L"--ks-key-alias", std::wstring{ expectedAlias.begin(), expectedAlias.end() },
        L"--ks-pass", L"stdin", L"--key-pass", L"stdin", L"--out", request->outputApk.wstring(), request->inputApk.wstring() };
    std::wstring command = Quote(trustedJava.wstring());
    for (const std::wstring& argument : arguments) { command.push_back(L' '); command += Quote(argument); }
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputRead.Get(); startup.StartupInfo.hStdOutput = outputWrite.Get(); startup.StartupInfo.hStdError = outputWrite.Get();
    SIZE_T attributeBytes = 0U;
    static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1U, 0U, &attributeBytes));
    std::vector<unsigned char> attributeStorage(attributeBytes);
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    std::array<HANDLE, 2> inheritedHandles{ inputRead.Get(), outputWrite.Get() };
    const bool attributeListInitialized =
        InitializeProcThreadAttributeList(startup.lpAttributeList, 1U, 0U, &attributeBytes) != FALSE;
    const bool attributesReady = attributeListInitialized &&
        UpdateProcThreadAttribute(startup.lpAttributeList, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritedHandles.data(), sizeof(inheritedHandles), nullptr, nullptr);
    std::optional<std::vector<wchar_t>> environment = package_process::BuildSanitizedEnvironment();
    if (!attributesReady || !environment.has_value()) {
        if (attributeListInitialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        clearSecrets(); result.message = "Signer process isolation could not be configured."; return result;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(trustedJava.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            environment->data(), jobRoot.c_str(), &startup.StartupInfo, &process)) {
        SecureZeroMemory(environment->data(), environment->size() * sizeof(wchar_t));
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        clearSecrets(); result.message = "ApkSignerTool could not be started."; return result;
    }
    SecureZeroMemory(environment->data(), environment->size() * sizeof(wchar_t));
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    ScopedHandle processHandle{ process.hProcess }, threadHandle{ process.hThread };
    if (processJob != nullptr && !AssignProcessToJobObject(static_cast<HANDLE>(processJob), processHandle.Get())) {
        TerminateProcess(processHandle.Get(), ERROR_PROCESS_ABORTED); clearSecrets();
        result.message = "ApkSignerTool could not join the package process job."; return result;
    }
    if (ResumeThread(threadHandle.Get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(processHandle.Get(), ERROR_PROCESS_ABORTED);
        clearSecrets();
        result.message = "ApkSignerTool process could not be resumed.";
        return result;
    }
    inputRead = ScopedHandle{};
    outputWrite = ScopedHandle{};
    const bool wrote = WriteSecret(inputWrite.Get(), storePassword) && WriteSecret(inputWrite.Get(), keyPassword);
    clearSecrets();
    inputWrite = ScopedHandle{};
    if (!wrote) TerminateProcess(processHandle.Get(), ERROR_WRITE_FAULT);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 180 };
    DWORD wait = WAIT_TIMEOUT;
    while ((wait = WaitForSingleObject(processHandle.Get(), 50U)) == WAIT_TIMEOUT && std::chrono::steady_clock::now() < deadline) {
        DWORD available = 0U;
        while (PeekNamedPipe(outputRead.Get(), nullptr, 0U, nullptr, &available, nullptr) && available > 0U) {
            std::array<char, 4096> buffer{}; DWORD read = 0U;
            if (!ReadFile(outputRead.Get(), buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &read, nullptr) || read == 0U) break;
            if (result.toolOutput.size() + read > kMaximumOutputBytes) { TerminateProcess(processHandle.Get(), ERROR_BUFFER_OVERFLOW); break; }
            result.toolOutput.append(buffer.data(), read);
        }
    }
    if (wait == WAIT_TIMEOUT) { TerminateProcess(processHandle.Get(), ERROR_TIMEOUT); WaitForSingleObject(processHandle.Get(), 5000U); }
    DWORD exitCode = 1U; GetExitCodeProcess(processHandle.Get(), &exitCode);
    result.succeeded = wrote && wait != WAIT_TIMEOUT && exitCode == 0U && std::filesystem::is_regular_file(request->outputApk) &&
        WriteResponse(responseFile, request->session);
    result.message = result.succeeded ? "Android release package signed." : "ApkSignerTool rejected or did not publish the signed package.";
    if (!result.succeeded) std::filesystem::remove(request->outputApk, filesystemError);
    return result;
#endif
}

} // namespace kb::editor
