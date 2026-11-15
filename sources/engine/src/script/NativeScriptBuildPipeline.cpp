#include "engine/script/NativeScriptBuildPipeline.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
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
    bool cancelled = false;
    std::string output;
    std::string partialLine;
};

// What a caller of a running build can see and do from another thread.
struct BuildControl {
    std::atomic<bool> cancel{ false };
    std::atomic<std::size_t> lines{ 0 };
    mutable std::mutex mutex;
    std::string lastLine;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

#if !defined(__EMSCRIPTEN__)
constexpr std::size_t kMaxCapturedBytes = 1U << 20U;

void KeepOutput(const char* data, std::size_t size, BuildRun& run, BuildControl* control) {
    if (run.output.size() < kMaxCapturedBytes) {
        run.output.append(data, std::min(size, kMaxCapturedBytes - run.output.size()));
    }
    if (control != nullptr) {
        const std::lock_guard lock{ control->mutex };
        for (std::size_t index = 0U; index < size; ++index) {
            if (data[index] == '\n') {
                if (!run.partialLine.empty()) control->lastLine = std::move(run.partialLine);
                run.partialLine.clear();
                control->lines.fetch_add(1U, std::memory_order_relaxed);
            } else if (data[index] != '\r' && run.partialLine.size() < kMaxReportedLineBytes) {
                run.partialLine.push_back(data[index]);
            }
        }
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

#if defined(_WIN32)
// vcvars64.bat looks for vswhere.exe on PATH; Visual Studio's installer puts it in a fixed folder that
// is on nobody's PATH, so every build printed "'vswhere.exe' is not recognized". Putting that folder in
// front of PATH for the build process only is the documented way to find it, and a build that really
// cannot find the compiler still fails the same way and says so.
[[nodiscard]] std::vector<wchar_t> BuildEnvironment() {
    wchar_t programFiles[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    const std::filesystem::path installer = std::filesystem::path{ programFiles } / L"Microsoft Visual Studio" / L"Installer";
    std::error_code error;
    if (!std::filesystem::is_regular_file(installer / L"vswhere.exe", error) || error) return {};
    LPWCH inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) return {};
    std::vector<wchar_t> block;
    bool pathFound = false;
    for (const wchar_t* entry = inherited; *entry != L'\0'; entry += std::wcslen(entry) + 1U) {
        const std::size_t entryLength = std::wcslen(entry);
        if (entryLength >= 5U && CompareStringOrdinal(entry, 5, L"PATH=", 5, TRUE) == CSTR_EQUAL) {
            pathFound = true;
            const std::wstring_view current{ entry + 5, entryLength - 5U };
            std::wstring replaced(entry, 5);
            if (current.find(installer.native()) == std::wstring_view::npos) {
                replaced += installer.native() + L";";
            }
            replaced += current;
            block.insert(block.end(), replaced.begin(), replaced.end());
            block.push_back(L'\0');
        } else {
            block.insert(block.end(), entry, entry + entryLength + 1U);
        }
    }
    FreeEnvironmentStringsW(inherited);
    if (!pathFound) return {};
    block.push_back(L'\0');
    return block;
}
#endif

// Compiler output is diagnostic output. Redirect only the child process so a build invoked by CLI JSON
// or MCP cannot write into the protocol stream.
// The child gets its own working directory and (when `control` asks) its own process tree to stop, so no
// other thread of this process notices the build: the process-wide current directory is never changed.
[[nodiscard]] BuildRun RunBuildCommand(const std::string& command, const std::filesystem::path& workingDirectory, BuildControl* control) {
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
    const std::vector<wchar_t> environment = BuildEnvironment();
    // Suspended until it is in its job, so nothing it starts first escapes the job and a cancel reaches all of it.
    const bool launched = listed &&
        CreateProcessW(shell.c_str(), arguments.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            environment.empty() ? nullptr : const_cast<wchar_t*>(environment.data()),
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup.StartupInfo, &process);
    if (listed) DeleteProcThreadAttributeList(attributes);
    if (input != INVALID_HANDLE_VALUE && input != nullptr) CloseHandle(input);
    CloseHandle(writeEnd);
    if (!launched) {
        CloseHandle(readEnd);
        return run;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr && !AssignProcessToJobObject(job, process.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    // Read until the child ends. A process the build left running keeps the pipe open, so the end of the
    // child, not the end of the pipe, stops the reading once what is already written is taken.
    char buffer[4096];
    for (;;) {
        if (control != nullptr && control->cancel.load() && !run.cancelled) {
            run.cancelled = true;
            if (job != nullptr) TerminateJobObject(job, 1);
            else TerminateProcess(process.hProcess, 1);
        }
        DWORD available = 0;
        if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available != 0) {
            DWORD count = 0;
            if (!ReadFile(readEnd, buffer, std::min(available, static_cast<DWORD>(sizeof(buffer))), &count, nullptr) || count == 0) break;
            KeepOutput(buffer, count, run, control);
        } else if (WaitForSingleObject(process.hProcess, 20) == WAIT_OBJECT_0) {
            if (!PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
        }
    }
    CloseHandle(readEnd);
    if (job != nullptr) CloseHandle(job);
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
    const std::string directory = workingDirectory.string();
    const pid_t child = fork();
    if (child < 0) {
        close(ends[0]);
        close(ends[1]);
        return run;
    }
    if (child == 0) {
        // Its own process group, so a cancel stops the compiler processes the shell started too.
        setpgid(0, 0);
        if (!directory.empty() && chdir(directory.c_str()) != 0) _exit(127);
        if (dup2(ends[1], STDOUT_FILENO) < 0 || dup2(ends[1], STDERR_FILENO) < 0) _exit(127);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    setpgid(child, child);
    close(ends[1]);
    char buffer[4096];
    for (;;) {
        if (control != nullptr && control->cancel.load() && !run.cancelled) {
            run.cancelled = true;
            kill(-child, SIGKILL);
        }
        pollfd waiting{ ends[0], POLLIN, 0 };
        const int ready = poll(&waiting, 1, 50);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (ready == 0) continue;
        const ssize_t count = read(ends[0], buffer, sizeof(buffer));
        if (count > 0) {
            KeepOutput(buffer, static_cast<std::size_t>(count), run, control);
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
    // WebAssembly has no processes to isolate: the build runs in this one, in the requested directory.
    std::error_code directoryError;
    const std::filesystem::path original = std::filesystem::current_path(directoryError);
    if (!workingDirectory.empty()) std::filesystem::current_path(workingDirectory, directoryError);
    run.exitCode = directoryError ? -1 : std::system(command.c_str());
    if (!original.empty()) std::filesystem::current_path(original, directoryError);
    static_cast<void>(control);
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

[[nodiscard]] NativeScriptBuildResult RunBuild(const NativeScriptBuildDesc& desc, BuildControl* control) {
    NativeScriptBuildResult result{};
    if (!desc.enabled) {
        return result;
    }
    if (desc.command.empty()) {
        result.errors.push_back("native script build command is empty");
        return result;
    }
    if (!desc.workingDirectory.empty()) {
        std::error_code error;
        if (!std::filesystem::is_directory(desc.workingDirectory, error) || error) {
            result.errors.push_back("native script build working directory could not be selected");
            return result;
        }
    }

    const BuildRun run = RunBuildCommand(desc.command, desc.workingDirectory, control);
    result.exitCode = run.exitCode;
    result.cancelled = run.cancelled;
    if (run.cancelled) {
        return result;
    }
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

} // namespace

std::optional<NativeScriptSourceLocation> ParseBuildMessageLocation(std::string_view message) {
    message = TrimLine(message);
    // A console row says what failed before the compiler's own text ("Name could not be loaded: path(12): ..."):
    // the path starts after the last ": " in front of the location.
    const auto pathBefore = [&message](std::size_t end) {
        std::string_view path = TrimLine(message.substr(0U, end));
        if (const std::size_t prefix = path.rfind(": "); prefix != std::string_view::npos) path.remove_prefix(prefix + 2U);
        return std::string{ TrimLine(path) };
    };
    const auto number = [&message](std::size_t& position) -> std::optional<int> {
        const std::size_t begin = position;
        long long value = 0;
        while (position < message.size() && IsDigit(message[position]) && value < 100000000LL) {
            value = value * 10 + (message[position] - '0');
            ++position;
        }
        return position == begin ? std::nullopt : std::optional<int>{ static_cast<int>(value) };
    };
    // "path(12): ..." and "path(12,5): ..."
    for (std::size_t open = message.find('('); open != std::string_view::npos && open != 0U; open = message.find('(', open + 1U)) {
        std::size_t position = open + 1U;
        const std::optional<int> line = number(position);
        if (!line.has_value()) continue;
        std::optional<int> column;
        if (position < message.size() && message[position] == ',') {
            ++position;
            column = number(position);
        }
        if (position < message.size() && message[position] == ')' && line.value() > 0) {
            return NativeScriptSourceLocation{
                .path = pathBefore(open), .line = line.value(), .column = column.value_or(0) };
        }
    }
    // "path:12: ...", "path:12:5: ..."; the colon of a drive letter ("C:\...") is not a separator.
    const std::size_t first = message.size() > 2U && message[1] == ':' && (message[2] == '\\' || message[2] == '/') ? 2U : 0U;
    for (std::size_t colon = message.find(':', first); colon != std::string_view::npos && colon != 0U; colon = message.find(':', colon + 1U)) {
        std::size_t position = colon + 1U;
        const std::optional<int> line = number(position);
        if (!line.has_value() || line.value() <= 0) continue;
        std::optional<int> column;
        if (position < message.size() && message[position] == ':') {
            std::size_t columnPosition = position + 1U;
            column = number(columnPosition);
            if (column.has_value()) position = columnPosition;
        }
        if (position == message.size() || message[position] == ':') {
            return NativeScriptSourceLocation{
                .path = pathBefore(colon), .line = line.value(), .column = column.value_or(0) };
        }
    }
    return std::nullopt;
}

NativeScriptBuildResult NativeScriptBuildPipeline::Build(const NativeScriptBuildDesc& desc) {
    return RunBuild(desc, nullptr);
}

struct NativeScriptBuildJob::State {
    BuildControl control;
    NativeScriptBuildResult result;
    std::atomic<bool> finished{ false };
    double elapsedAtEnd = 0.0;
    std::thread worker;
};

NativeScriptBuildJob::NativeScriptBuildJob(NativeScriptBuildDesc desc)
    : state_(std::make_unique<State>()) {
    State* const state = state_.get();
    state->worker = std::thread([state, desc = std::move(desc)] {
        state->result = RunBuild(desc, &state->control);
        state->elapsedAtEnd = std::chrono::duration<double>(std::chrono::steady_clock::now() - state->control.started).count();
        state->finished.store(true);
    });
}

NativeScriptBuildJob::~NativeScriptBuildJob() {
    if (state_ == nullptr) return;
    state_->control.cancel.store(true);
    if (state_->worker.joinable()) state_->worker.join();
}

bool NativeScriptBuildJob::Finished() const noexcept {
    return state_->finished.load();
}

NativeScriptBuildProgress NativeScriptBuildJob::Progress() const {
    NativeScriptBuildProgress progress;
    progress.finished = state_->finished.load();
    progress.cancelRequested = state_->control.cancel.load();
    progress.elapsedSeconds = progress.finished ? state_->elapsedAtEnd
        : std::chrono::duration<double>(std::chrono::steady_clock::now() - state_->control.started).count();
    progress.outputLines = state_->control.lines.load();
    const std::lock_guard lock{ state_->control.mutex };
    progress.lastLine = state_->control.lastLine;
    return progress;
}

void NativeScriptBuildJob::Cancel() noexcept {
    state_->control.cancel.store(true);
}

NativeScriptBuildResult NativeScriptBuildJob::Wait() {
    if (state_->worker.joinable()) state_->worker.join();
    return state_->result;
}

} // namespace kb::script
