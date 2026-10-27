#include "engine/script/NativeScriptBuildPipeline.hpp"

#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kb::script {
namespace {

// Compiler output is diagnostic output. Redirect only the child process so a
// build invoked by CLI JSON or MCP cannot write into the protocol stream.
[[nodiscard]] int RunBuildCommand(const std::string& command) {
#if defined(_WIN32)
    if (command.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return -1;
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.data(),
        static_cast<int>(command.size()), nullptr, 0);
    if (size == 0) return -1;
    std::wstring text(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.data(),
            static_cast<int>(command.size()), text.data(), size) == 0) return -1;
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return -1;
    const std::wstring shell = std::wstring{directory, length} + L"\\cmd.exe";
    std::wstring arguments = L"\"" + shell + L"\" /D /S /C \"" + text + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(shell.c_str(), arguments.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return -1;
    CloseHandle(process.hThread);
    DWORD exitCode = 1;
    const bool completed = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return completed && exitCode <= static_cast<DWORD>(std::numeric_limits<int>::max())
        ? static_cast<int>(exitCode) : -1;
#elif !defined(__EMSCRIPTEN__)
    const pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) _exit(127);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
#else
    return std::system(command.c_str());
#endif
}

class ScopedWorkingDirectory final {
public:
    explicit ScopedWorkingDirectory(const std::filesystem::path& workingDirectory, std::vector<std::string>& errors)
        : errors_(errors) {
        std::error_code error;
        originalDirectory_ = std::filesystem::current_path(error);
        if (error) {
            errors_.push_back("native script build could not read current working directory");
            return;
        }
        active_ = true;
        if (!workingDirectory.empty()) {
            std::filesystem::current_path(workingDirectory, error);
            if (error) {
                errors_.push_back("native script build working directory could not be selected");
                active_ = false;
            }
        }
    }

    ~ScopedWorkingDirectory() {
        if (!active_) {
            return;
        }
        std::error_code error;
        std::filesystem::current_path(originalDirectory_, error);
        if (error) {
            errors_.push_back("native script build could not restore working directory");
        }
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory&) = delete;
    ScopedWorkingDirectory& operator=(const ScopedWorkingDirectory&) = delete;

    [[nodiscard]] bool Succeeded() const noexcept {
        return active_;
    }

private:
    std::filesystem::path originalDirectory_;
    std::vector<std::string>& errors_;
    bool active_ = false;
};

} // namespace

NativeScriptBuildResult NativeScriptBuildPipeline::Build(const NativeScriptBuildDesc& desc) {
    NativeScriptBuildResult result{};
    if (!desc.enabled) {
        return result;
    }
    if (desc.command.empty()) {
        result.errors.push_back("native script build command is empty");
        return result;
    }

    ScopedWorkingDirectory workingDirectory{ desc.workingDirectory, result.errors };
    if (!workingDirectory.Succeeded()) {
        return result;
    }
    result.exitCode = RunBuildCommand(desc.command);
    if (result.exitCode != 0) {
        result.errors.push_back("native script build command failed with exit code " + std::to_string(result.exitCode) +
            "; read the compiler output above. If a tool is missing, install the C++ compiler, CMake and the "
            "configured build tool, then regenerate the build command in the .native descriptor and retry script-build.");
    }
    return result;
}

} // namespace kb::script
