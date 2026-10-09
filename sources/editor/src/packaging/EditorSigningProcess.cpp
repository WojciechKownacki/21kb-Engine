#include "packaging/EditorSigningProcess.hpp"

#include "packaging/EditorPackageProcessEnvironment.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <system_error>

namespace kb::editor::signing_process {

std::filesystem::path Canonical(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
    return error ? std::filesystem::path{} : result;
}

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept {
    const auto lhs = Canonical(left);
    const auto rhs = Canonical(right);
    if (lhs.empty() || rhs.empty()) return false;
#if defined(_WIN32)
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
#else
    return lhs == rhs;
#endif
}

bool DirectChildOf(const std::filesystem::path& root, const std::filesystem::path& child) noexcept {
    return !Canonical(root).empty() && SamePath(Canonical(child).parent_path(), root);
}

bool ValidSession(std::string_view value) noexcept {
    return value.size() == 32U && std::ranges::all_of(value, [](char character) {
        return std::isdigit(static_cast<unsigned char>(character)) != 0 || (character >= 'a' && character <= 'f');
    });
}

void SecureClear(std::string& value) noexcept {
    if (value.capacity() > value.size()) value.resize(value.capacity(), '\0');
    volatile char* bytes = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0U; index < value.size(); ++index) bytes[index] = '\0';
    value.clear();
}

#if defined(_WIN32)
ScopedHandle Guard(const std::filesystem::path& path, bool directory, DWORD access, DWORD share) noexcept {
    ScopedHandle handle{ CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0U), nullptr) };
    if (!handle) return {};
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(handle.Get(), FileAttributeTagInfo, &info, sizeof(info)) ||
        (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        (((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) != directory)) return {};
    return handle;
}

std::wstring Quote(std::wstring_view argument) {
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

namespace {

[[nodiscard]] bool WriteSecret(HANDLE pipe, std::string_view secret) noexcept {
    DWORD written = 0U;
    if (!WriteFile(pipe, secret.data(), static_cast<DWORD>(secret.size()), &written, nullptr) || written != secret.size()) return false;
    constexpr char newline = '\n';
    return WriteFile(pipe, &newline, 1U, &written, nullptr) && written == 1U;
}

void ClearSecrets(std::vector<std::string*>& secrets) noexcept {
    for (std::string* secret : secrets) {
        if (secret != nullptr) SecureClear(*secret);
    }
}

} // namespace

SignerRun RunSigner(
    const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
    const std::filesystem::path& workingDirectory, std::vector<std::string*> secrets, void* processJob,
    std::chrono::seconds timeout, std::size_t maximumOutputBytes) {
    SignerRun run;
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE inputReadRaw = nullptr, inputWriteRaw = nullptr, outputReadRaw = nullptr, outputWriteRaw = nullptr;
    if (!CreatePipe(&inputReadRaw, &inputWriteRaw, &security, 0U) || !CreatePipe(&outputReadRaw, &outputWriteRaw, &security, 0U) ||
        !SetHandleInformation(inputWriteRaw, HANDLE_FLAG_INHERIT, 0U) || !SetHandleInformation(outputReadRaw, HANDLE_FLAG_INHERIT, 0U)) {
        if (inputReadRaw) CloseHandle(inputReadRaw); if (inputWriteRaw) CloseHandle(inputWriteRaw);
        if (outputReadRaw) CloseHandle(outputReadRaw); if (outputWriteRaw) CloseHandle(outputWriteRaw);
        ClearSecrets(secrets); run.error = "Secure signer pipes could not be created."; return run;
    }
    ScopedHandle inputRead{ inputReadRaw }, inputWrite{ inputWriteRaw }, outputRead{ outputReadRaw }, outputWrite{ outputWriteRaw };
    std::wstring command = Quote(executable.wstring());
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
        ClearSecrets(secrets); run.error = "Signer process isolation could not be configured."; return run;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
            environment->data(), workingDirectory.c_str(), &startup.StartupInfo, &process)) {
        SecureZeroMemory(environment->data(), environment->size() * sizeof(wchar_t));
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        ClearSecrets(secrets); run.error = "The signer could not be started."; return run;
    }
    SecureZeroMemory(environment->data(), environment->size() * sizeof(wchar_t));
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    ScopedHandle processHandle{ process.hProcess }, threadHandle{ process.hThread };
    if (processJob != nullptr && !AssignProcessToJobObject(static_cast<HANDLE>(processJob), processHandle.Get())) {
        TerminateProcess(processHandle.Get(), ERROR_PROCESS_ABORTED); ClearSecrets(secrets);
        run.error = "The signer could not join the package process job."; return run;
    }
    if (ResumeThread(threadHandle.Get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(processHandle.Get(), ERROR_PROCESS_ABORTED);
        ClearSecrets(secrets);
        run.error = "The signer process could not be resumed.";
        return run;
    }
    run.started = true;
    inputRead = ScopedHandle{};
    outputWrite = ScopedHandle{};
    run.secretsWritten = std::ranges::all_of(secrets, [&](const std::string* secret) {
        return secret != nullptr && WriteSecret(inputWrite.Get(), *secret);
    });
    ClearSecrets(secrets);
    inputWrite = ScopedHandle{};
    if (!run.secretsWritten) TerminateProcess(processHandle.Get(), ERROR_WRITE_FAULT);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    DWORD wait = WAIT_TIMEOUT;
    const auto drain = [&]() {
        DWORD available = 0U;
        while (PeekNamedPipe(outputRead.Get(), nullptr, 0U, nullptr, &available, nullptr) && available > 0U) {
            std::array<char, 4096> buffer{}; DWORD read = 0U;
            if (!ReadFile(outputRead.Get(), buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &read, nullptr) || read == 0U) break;
            if (run.output.size() + read > maximumOutputBytes) { TerminateProcess(processHandle.Get(), ERROR_BUFFER_OVERFLOW); break; }
            run.output.append(buffer.data(), read);
        }
    };
    while ((wait = WaitForSingleObject(processHandle.Get(), 50U)) == WAIT_TIMEOUT && std::chrono::steady_clock::now() < deadline) {
        drain();
    }
    if (wait == WAIT_TIMEOUT) {
        run.timedOut = true;
        TerminateProcess(processHandle.Get(), ERROR_TIMEOUT);
        WaitForSingleObject(processHandle.Get(), 5000U);
    }
    drain();
    GetExitCodeProcess(processHandle.Get(), &run.exitCode);
    return run;
}
#endif

} // namespace kb::editor::signing_process
