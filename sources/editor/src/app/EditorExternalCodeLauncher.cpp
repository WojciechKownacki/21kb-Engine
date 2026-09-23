#include "app/EditorExternalCodeLauncher.hpp"

#include <filesystem>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {
namespace {

#if defined(_WIN32)
[[nodiscard]] std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0U) {
        return {};
    }
    std::wstring value(required, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0U || written >= required) {
        return {};
    }
    value.resize(written);
    return value;
}

[[nodiscard]] std::filesystem::path FindCodeExecutable() {
    for (const wchar_t* variable : { L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)" }) {
        const std::wstring root = EnvironmentValue(variable);
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path candidate = std::filesystem::path{ root } / "Programs" / "Microsoft VS Code" / "Code.exe";
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
        const std::filesystem::path machineCandidate = std::filesystem::path{ root } / "Microsoft VS Code" / "Code.exe";
        if (std::filesystem::is_regular_file(machineCandidate, error) && !error) {
            return machineCandidate;
        }
    }
    const std::wstring pathVariable = EnvironmentValue(L"PATH");
    std::size_t begin = 0;
    while (begin <= pathVariable.size()) {
        const std::size_t end = pathVariable.find(L';', begin);
        std::wstring_view entry{ pathVariable.data() + begin,
            (end == std::wstring::npos ? pathVariable.size() : end) - begin };
        if (entry.size() >= 2U && entry.front() == L'"' && entry.back() == L'"') {
            entry.remove_prefix(1U);
            entry.remove_suffix(1U);
        }
        if (!entry.empty()) {
            const std::filesystem::path directory{ entry };
            if (directory.is_absolute()) {
                std::error_code error;
                const std::filesystem::path direct = directory / "Code.exe";
                if (std::filesystem::is_regular_file(direct, error) && !error) {
                    return direct;
                }
                const std::filesystem::path fromCli = directory.parent_path() / "Code.exe";
                if (std::filesystem::is_regular_file(directory / "code.cmd", error) && !error &&
                    std::filesystem::is_regular_file(fromCli, error) && !error) {
                    return fromCli;
                }
            }
        }
        if (end == std::wstring::npos) {
            break;
        }
        begin = end + 1U;
    }
    return {};
}

[[nodiscard]] std::wstring QuotePath(const std::filesystem::path& path) {
    std::wstring quoted = L"\"" + path.wstring();
    if (!quoted.empty() && quoted.back() == L'\\') {
        quoted.push_back(L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

[[nodiscard]] std::vector<wchar_t> ChildEnvironment() {
    LPWCH inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) {
        return {};
    }
    std::vector<wchar_t> environment;
    for (const wchar_t* entry = inherited; *entry != L'\0'; entry += std::wcslen(entry) + 1U) {
        const std::size_t length = std::wcslen(entry);
        const bool cliNodeMode = length >= 21U &&
            CompareStringOrdinal(entry, 21, L"ELECTRON_RUN_AS_NODE=", 21, TRUE) == CSTR_EQUAL;
        const bool developerMode = length >= 11U &&
            CompareStringOrdinal(entry, 11, L"VSCODE_DEV=", 11, TRUE) == CSTR_EQUAL;
        if (!cliNodeMode && !developerMode) {
            environment.insert(environment.end(), entry, entry + length + 1U);
        }
    }
    FreeEnvironmentStringsW(inherited);
    if (environment.empty()) {
        environment.push_back(L'\0');
    }
    environment.push_back(L'\0');
    return environment;
}
#endif

} // namespace

bool EditorExternalCodeLauncher::OpenProjectFile(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& filePath,
    std::string& error) {
    error.clear();
#if defined(_WIN32)
    if (projectRoot.empty() || filePath.empty()) {
        error = "Project folder or script file is unavailable.";
        return false;
    }
    std::error_code fileError;
    const std::filesystem::path absoluteProjectRoot = std::filesystem::absolute(projectRoot, fileError);
    if (fileError) {
        error = "Project folder is unavailable.";
        return false;
    }
    const std::filesystem::path absoluteFilePath = std::filesystem::absolute(filePath, fileError);
    if (fileError || !std::filesystem::is_directory(absoluteProjectRoot, fileError) || fileError ||
        !std::filesystem::is_regular_file(absoluteFilePath, fileError) || fileError) {
        error = "Project folder or script file is unavailable.";
        return false;
    }
    const std::filesystem::path executable = FindCodeExecutable();
    if (executable.empty()) {
        error = "VS Code was not found. Install it or add its 'code' command to PATH.";
        return false;
    }

    std::wstring command = QuotePath(executable) + L" "
        + QuotePath(absoluteProjectRoot) + L" " + QuotePath(absoluteFilePath);
    std::vector<wchar_t> environment = ChildEnvironment();
    if (environment.empty()) {
        error = "VS Code environment could not be prepared.";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, environment.data(), absoluteProjectRoot.c_str(), &startup, &process);
    if (created == FALSE) {
        error = "VS Code could not be started (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    static_cast<void>(projectRoot);
    static_cast<void>(filePath);
    error = "External code editor integration is unavailable on this platform.";
    return false;
#endif
}

} // namespace kb::editor
