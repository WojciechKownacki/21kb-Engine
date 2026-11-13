#include "engine/script/NativeScriptBuildPipeline.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kb::script {
namespace {

// What a build printed. Its messages are the only way a user learns why a script did not compile, and an
// editor has no console to read them from, so they are kept (up to a bound) and also passed on to this
// process's stderr as they arrive, which keeps a command line caller's live output unchanged.
constexpr std::size_t kMaxReportedLines = 12U;
constexpr std::size_t kMaxReportedLineBytes = 400U;

struct BuildRun {
    int exitCode = -1;
    std::string output;
};

#if !defined(__EMSCRIPTEN__)
constexpr std::size_t kMaxCapturedBytes = 1U << 20U;

void KeepOutput(const char* data, std::size_t size, BuildRun& run) {
    if (run.output.size() < kMaxCapturedBytes) {
        run.output.append(data, std::min(size, kMaxCapturedBytes - run.output.size()));
    }
#if defined(_WIN32)
    const HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);
    if (standardError != nullptr && standardError != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(standardError, data, static_cast<DWORD>(size), &written, nullptr);
    }
#else
    const ssize_t written = write(STDERR_FILENO, data, size);
    static_cast<void>(written);
#endif
}
#endif

// Compiler output is diagnostic output. Redirect only the child process so a build invoked by CLI JSON
// or MCP cannot write into the protocol stream.
[[nodiscard]] BuildRun RunBuildCommand(const std::string& command) {
    BuildRun run;
#if defined(_WIN32)
    if (command.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return run;
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.data(),
        static_cast<int>(command.size()), nullptr, 0);
    if (size == 0) return run;
    std::wstring text(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, command.data(),
            static_cast<int>(command.size()), text.data(), size) == 0) return run;
    wchar_t directory[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return run;
    const std::wstring shell = std::wstring{directory, length} + L"\\cmd.exe";
    std::wstring arguments = L"\"" + shell + L"\" /D /S /C \"" + text + L"\"";
    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) return run;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = nullptr;
    const HANDLE source = GetStdHandle(STD_INPUT_HANDLE);
    if (source == nullptr || source == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &input, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        input = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    // Only these two handles go to the child: a pipe end that another process started meanwhile also
    // inherited would keep the read below waiting until that process ends.
    HANDLE inherited[2] = {input, writeEnd};
    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
    std::vector<char> listStorage(listSize);
    auto* const attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listStorage.data());
    const bool listed = input != INVALID_HANDLE_VALUE && listSize != 0 &&
        InitializeProcThreadAttributeList(attributes, 1, 0, &listSize) &&
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited),
            nullptr, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = writeEnd;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    const bool launched = listed &&
        CreateProcessW(shell.c_str(), arguments.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process);
    if (listed) DeleteProcThreadAttributeList(attributes);
    if (input != INVALID_HANDLE_VALUE && input != nullptr) CloseHandle(input);
    CloseHandle(writeEnd);
    if (!launched) {
        CloseHandle(readEnd);
        return run;
    }
    CloseHandle(process.hThread);
    // Read until the child ends. A process the build left running keeps the pipe open, so the end of the
    // child, not the end of the pipe, stops the reading once what is already written is taken.
    char buffer[4096];
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available != 0) {
            DWORD count = 0;
            if (!ReadFile(readEnd, buffer, std::min(available, static_cast<DWORD>(sizeof(buffer))), &count, nullptr) || count == 0) break;
            KeepOutput(buffer, count, run);
        } else if (WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0) {
            if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
        }
    }
    CloseHandle(readEnd);
    DWORD exitCode = 1;
    const bool completed = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    run.exitCode = completed && exitCode <= static_cast<DWORD>(std::numeric_limits<int>::max())
        ? static_cast<int>(exitCode) : -1;
#elif !defined(__EMSCRIPTEN__)
    int ends[2]{};
    if (pipe(ends) != 0) return run;
    // Close-on-exec keeps a build another thread starts from holding this pipe open.
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    const pid_t child = fork();
    if (child < 0) {
        close(ends[0]);
        close(ends[1]);
        return run;
    }
    if (child == 0) {
        if (dup2(ends[1], STDOUT_FILENO) < 0 || dup2(ends[1], STDERR_FILENO) < 0) _exit(127);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(ends[1]);
    char buffer[4096];
    for (;;) {
        const ssize_t count = read(ends[0], buffer, sizeof(buffer));
        if (count > 0) {
            KeepOutput(buffer, static_cast<std::size_t>(count), run);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(ends[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited < 0) return run;
    if (WIFEXITED(status)) run.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) run.exitCode = 128 + WTERMSIG(status);
#else
    run.exitCode = std::system(command.c_str());
#endif
    return run;
}

[[nodiscard]] std::string_view TrimLine(std::string_view line) noexcept {
    const auto blank = [](char character) noexcept {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    while (!line.empty() && blank(line.back())) line.remove_suffix(1);
    while (!line.empty() && blank(line.front())) line.remove_prefix(1);
    return line;
}

[[nodiscard]] bool IsDigit(char character) noexcept {
    return character >= '0' && character <= '9';
}

// "path(12)", "path(12,5)", "path:12" or "path:12:5": where a compiler says the problem is.
[[nodiscard]] bool EndsWithLocation(std::string_view head) noexcept {
    if (head.empty()) return false;
    if (head.back() == ')') {
        const std::size_t open = head.rfind('(');
        if (open == std::string_view::npos || open + 2U >= head.size()) return false;
        const std::string_view inside = head.substr(open + 1U, head.size() - open - 2U);
        return IsDigit(inside.front()) && std::ranges::all_of(inside, [](char character) noexcept {
            return IsDigit(character) || character == ',';
        });
    }
    std::size_t digits = 0;
    while (digits < head.size() && IsDigit(head[head.size() - 1U - digits])) ++digits;
    return digits > 0 && digits < head.size() && head[head.size() - 1U - digits] == ':';
}

enum class MessageKind { Other, Warning, Error };

// "path(12): error C2065: ..." (MSVC), "path:12:5: error: ..." (Clang, GCC), "x.obj : error LNK2019: ...".
[[nodiscard]] MessageKind ClassifyBuildLine(std::string_view line) noexcept {
    if (line.find(": error") != std::string_view::npos || line.find(": fatal error") != std::string_view::npos) {
        return MessageKind::Error;
    }
    const std::size_t warning = line.find(": warning");
    return warning != std::string_view::npos && EndsWithLocation(line.substr(0, warning))
        ? MessageKind::Warning : MessageKind::Other;
}

struct BuildMessages {
    std::vector<std::string> lines;
    std::size_t errors = 0;
    std::size_t warnings = 0;
};

// The compiler's own error lines (file and line included), or, when it printed none in a known shape
// (CMake, a missing tool), the last lines it said.
[[nodiscard]] BuildMessages ReadBuildMessages(const std::string& output) {
    BuildMessages messages;
    std::vector<std::string> tail;
    std::size_t omitted = 0;
    for (std::size_t begin = 0; begin < output.size();) {
        std::size_t end = output.find('\n', begin);
        if (end == std::string::npos) end = output.size();
        std::string_view line = TrimLine(std::string_view{output}.substr(begin, end - begin));
        begin = end + 1U;
        if (line.empty()) continue;
        if (line.size() > kMaxReportedLineBytes) line = line.substr(0, kMaxReportedLineBytes);
        const MessageKind kind = ClassifyBuildLine(line);
        if (kind == MessageKind::Warning) {
            ++messages.warnings;
        } else if (kind == MessageKind::Error) {
            ++messages.errors;
            if (std::ranges::find(messages.lines, line) != messages.lines.end()) continue;
            if (messages.lines.size() < kMaxReportedLines) messages.lines.emplace_back(line);
            else ++omitted;
        }
        tail.emplace_back(line);
        if (tail.size() > kMaxReportedLines) tail.erase(tail.begin());
    }
    if (messages.lines.empty()) {
        messages.lines = std::move(tail);
    } else if (omitted != 0) {
        messages.lines.push_back("... and " + std::to_string(omitted) + " more");
    }
    return messages;
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
    const BuildRun run = RunBuildCommand(desc.command);
    result.exitCode = run.exitCode;
    if (result.exitCode != 0) {
        const BuildMessages messages = ReadBuildMessages(run.output);
        std::string counts;
        if (messages.errors != 0) {
            counts = " (" + std::to_string(messages.errors) + (messages.errors == 1 ? " error" : " errors") +
                (messages.warnings == 0 ? std::string{} : ", " + std::to_string(messages.warnings) +
                    (messages.warnings == 1 ? " warning" : " warnings")) + ")";
        }
        // The first entry says what happened; the rest are the compiler's lines, file and line first.
        result.errors.push_back("native script build command failed with exit code " + std::to_string(result.exitCode) +
            counts + "; the compiler messages follow. If a tool is missing, install the C++ compiler, CMake and the "
            "configured build tool, then regenerate the build command in the .native descriptor and retry script-build.");
        result.errors.insert(result.errors.end(), messages.lines.begin(), messages.lines.end());
    }
    return result;
}

} // namespace kb::script
