#include "app/EditorExternalCodeLauncher.hpp"
#include "engine/config/IniDocument.hpp"
#include "engine/assets/AssetFileName.hpp"

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

[[nodiscard]] bool ValidPreference(std::string_view value) noexcept {
    return value == "Auto" || value == "VSCode" || value == "VisualStudio";
}

[[nodiscard]] bool LoadPreferences(const std::filesystem::path& projectRoot,
    kb::config::IniDocument& document, std::string& error) {
    error.clear();
    if (projectRoot.empty()) {
        error = "Project folder is unavailable.";
        return false;
    }
    const auto file = projectRoot / "Config/EditorSettings.ini";
    std::error_code ec;
    const bool exists = std::filesystem::exists(file, ec);
    if (ec) { error = "Editor settings are unavailable: " + ec.message(); return false; }
    return !exists || document.Load(file, error);
}

#if defined(_WIN32)
thread_local EditorExternalCodeLauncher::ProcessLauncher g_processLauncher = nullptr;
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

[[nodiscard]] std::filesystem::path FindVisualStudioExecutable() {
    const auto valid = [](const std::filesystem::path& path) {
        std::error_code ec;
        return path.is_absolute() && std::filesystem::is_regular_file(path, ec) && !ec;
    };
    const std::wstring installed = EnvironmentValue(L"VSINSTALLDIR");
    if (!installed.empty()) {
        const auto candidate = std::filesystem::path{ installed } / "Common7/IDE/devenv.exe";
        if (valid(candidate)) return candidate;
    }
    const std::wstring programFiles = EnvironmentValue(L"ProgramFiles(x86)");
    if (programFiles.empty()) return {};
    const auto vswhere = std::filesystem::path{ programFiles } / "Microsoft Visual Studio/Installer/vswhere.exe";
    if (!valid(vswhere)) return {};
    SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 0)) return {};
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION process{};
    std::wstring command = QuotePath(vswhere) + L" -latest -property installationPath -utf8";
    const BOOL created = CreateProcessW(vswhere.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(write);
    std::string output;
    if (created) {
        if (WaitForSingleObject(process.hProcess, 5000U) == WAIT_OBJECT_0) {
            DWORD available = 0, count = 0;
            if (PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available > 0 && available < 32768U) {
                output.resize(available);
                if (!ReadFile(read, output.data(), available, &count, nullptr)) count = 0;
                output.resize(count);
            }
        } else {
            TerminateProcess(process.hProcess, 1);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    CloseHandle(read);
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n')) output.pop_back();
    if (output.empty()) return {};
    const auto candidate = kb::assets::Utf8Path(output) / "Common7/IDE/devenv.exe";
    return valid(candidate) ? candidate : std::filesystem::path{};
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

std::string EditorExternalCodeLauncher::Preference(const std::filesystem::path& projectRoot, std::string& error) {
    kb::config::IniDocument document;
    if (!LoadPreferences(projectRoot, document, error)) return {};
    const std::string value{ document.GetString("SourceCode", "Editor").value_or("Auto") };
    if (!ValidPreference(value)) { error = "SourceCode.Editor must be Auto, VSCode or VisualStudio."; return {}; }
    return value;
}

bool EditorExternalCodeLauncher::SetPreference(const std::filesystem::path& projectRoot,
    std::string_view preference, std::string& error) {
    error.clear();
    if (!ValidPreference(preference)) { error = "SourceCode.Editor must be Auto, VSCode or VisualStudio."; return false; }
    kb::config::IniDocument document;
    if (!LoadPreferences(projectRoot, document, error)) return false;
    document.SetString("SourceCode", "Editor", std::string{ preference });
    return document.Save(projectRoot / "Config/EditorSettings.ini", error);
}

#if defined(_WIN32)
EditorExternalCodeLauncher::ProcessLauncher EditorExternalCodeLauncher::ExchangeProcessLauncher(ProcessLauncher launcher) noexcept {
    const auto previous = g_processLauncher;
    g_processLauncher = launcher;
    return previous;
}
#endif

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
    const std::string preference = Preference(absoluteProjectRoot, error);
    if (preference.empty()) return false;
    bool visualStudio = preference == "VisualStudio";
    std::filesystem::path executable = visualStudio ? FindVisualStudioExecutable() : FindCodeExecutable();
    if (executable.empty() && preference == "Auto") {
        executable = FindVisualStudioExecutable();
        visualStudio = !executable.empty();
    }
    if (executable.empty()) {
        error = preference == "VisualStudio" ? "Visual Studio was not found. Install the C++ IDE or choose VS Code in Editor Settings."
            : preference == "VSCode" ? "VS Code was not found. Install it or choose Visual Studio in Editor Settings."
            : "No supported code editor was found. Install Visual Studio or VS Code and select it in Editor Settings.";
        return false;
    }

    std::filesystem::path workspace = absoluteProjectRoot;
    for (auto folder = absoluteFilePath.parent_path(); !folder.empty(); folder = folder.parent_path()) {
        const auto relative = folder.lexically_relative(absoluteProjectRoot);
        if (relative.empty() || *relative.begin() == "..") break;
        if (std::filesystem::is_regular_file(folder / "CMakeLists.txt", fileError) && !fileError) { workspace = folder; break; }
        if (folder == absoluteProjectRoot || folder == folder.parent_path()) break;
    }
    std::wstring command = QuotePath(executable) + L" "
        + (visualStudio ? std::wstring{} : L"--reuse-window ") + QuotePath(workspace)
        + (visualStudio ? L" /Edit " : L" --goto ") + QuotePath(absoluteFilePath);
    if (g_processLauncher != nullptr) return g_processLauncher(executable, command, workspace, error);
    std::vector<wchar_t> environment = ChildEnvironment();
    if (environment.empty()) {
        error = "Code editor environment could not be prepared.";
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, environment.data(), workspace.c_str(), &startup, &process);
    if (created == FALSE) {
        error = "Code editor could not be started (Windows error " + std::to_string(GetLastError()) + ").";
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
