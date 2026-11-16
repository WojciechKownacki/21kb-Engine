#include "project/EditorProjectRepository.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::editor {
namespace {

constexpr DWORD kQuickTimeoutMs = 15000U;
constexpr DWORD kReachTimeoutMs = 30000U;
constexpr DWORD kSendTimeoutMs = 600000U;
constexpr DWORD kSignInTimeoutMs = 600000U;

struct ProcessResult {
    bool started = false;
    bool finished = false;
    DWORD exitCode = 1U;
    std::string output;

    [[nodiscard]] bool Succeeded() const noexcept {
        return started && finished && exitCode == 0U;
    }
};

[[nodiscard]] std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            quoted.push_back(character);
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes + 1U, L'\\');
        }
        backslashes = 0U;
        quoted.push_back(character);
    }
    quoted.append(backslashes, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

[[nodiscard]] std::wstring CommandLine(std::string_view program, const std::vector<std::string>& arguments) {
    std::wstring line = ScriptEditorTextEncoding::Widen(program);
    for (const std::string& argument : arguments) {
        line += L' ';
        line += QuoteArgument(ScriptEditorTextEncoding::Widen(argument));
    }
    return line;
}

// Runs a program without a window and hands back everything it printed.
// `mergeErrors` false drops what the program says on its error stream, for output that is read as data.
[[nodiscard]] ProcessResult Run(std::string_view program, const std::vector<std::string>& arguments,
    const std::filesystem::path& workingDirectory, DWORD timeoutMs, bool mergeErrors = true) {
    // Nothing is there to answer a credentials prompt; asking would only hang until the timeout.
    SetEnvironmentVariableW(L"GIT_TERMINAL_PROMPT", L"0");

    ProcessResult result;
    SECURITY_ATTRIBUTES inheritable{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (CreatePipe(&readEnd, &writeEnd, &inheritable, 0U) == FALSE) {
        return result;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0U);
    HANDLE noInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0U, nullptr);
    HANDLE errorSink = mergeErrors ? INVALID_HANDLE_VALUE
        : CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0U, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = noInput;
    startup.hStdOutput = writeEnd;
    startup.hStdError = mergeErrors ? writeEnd : errorSink;
    PROCESS_INFORMATION process{};
    std::wstring line = CommandLine(program, arguments);
    const std::wstring directory = workingDirectory.wstring();
    result.started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        directory.empty() ? nullptr : directory.c_str(), &startup, &process) != FALSE;
    CloseHandle(writeEnd);
    if (noInput != INVALID_HANDLE_VALUE) {
        CloseHandle(noInput);
    }
    if (errorSink != INVALID_HANDLE_VALUE) {
        CloseHandle(errorSink);
    }
    if (!result.started) {
        CloseHandle(readEnd);
        return result;
    }

    const auto drain = [&]() {
        DWORD available = 0U;
        while (PeekNamedPipe(readEnd, nullptr, 0U, nullptr, &available, nullptr) != FALSE && available > 0U) {
            std::array<char, 4096> buffer{};
            DWORD read = 0U;
            if (ReadFile(readEnd, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == FALSE || read == 0U) {
                return;
            }
            result.output.append(buffer.data(), read);
        }
    };
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (true) {
        const DWORD waited = WaitForSingleObject(process.hProcess, 40U);
        drain();
        if (waited == WAIT_OBJECT_0) {
            result.finished = GetExitCodeProcess(process.hProcess, &result.exitCode) != FALSE;
            break;
        }
        if (GetTickCount64() >= deadline) {
            TerminateProcess(process.hProcess, 1U);
            WaitForSingleObject(process.hProcess, 2000U);
            break;
        }
    }
    CloseHandle(readEnd);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
}

[[nodiscard]] std::string Trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    std::size_t first = 0U;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }
    return text.substr(first);
}

[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] EditorRepositoryResult Failed(std::string message) {
    return { false, std::move(message) };
}

// What went wrong, as the program said it: its last line, which is where git and gh put the reason.
[[nodiscard]] EditorRepositoryResult FailedRun(const ProcessResult& run, std::string_view program) {
    if (!run.started) {
        return Failed(EditorLocalization::Format("repository.programMissing",
            "{program} is not installed on this computer.", { { "program", std::string{ program } } }));
    }
    if (!run.finished) {
        return Failed(std::string{ EditorText("repository.timedOut", "It took too long and was stopped. Check the connection and try again.") });
    }
    if (run.output.find("Please tell me who you are") != std::string::npos || run.output.find("user.email") != std::string::npos) {
        return Failed(std::string{ EditorText("repository.noIdentity",
            "Git does not know your name and e-mail yet. Set them with git config --global user.name and user.email.") });
    }
    std::string text = Trim(run.output);
    if (const std::size_t lineBreak = text.find_last_of('\n'); lineBreak != std::string::npos) {
        text = Trim(text.substr(lineBreak + 1U));
    }
    if (text.size() > 220U) {
        text.resize(220U);
    }
    return Failed(text.empty() ? std::string{ EditorText("repository.failed", "The command failed.") } : text);
}

void WriteDefaultIgnoreFile(const std::filesystem::path& projectRoot) {
    std::error_code error;
    const std::filesystem::path file = projectRoot / ".gitignore";
    if (std::filesystem::exists(file, error)) {
        return;
    }
    std::ofstream output(file, std::ios::binary);
    output << "# What the editor rebuilds by itself.\nLibrary/\nTemp/\nSaved/\nBuilds/\n*.tmp\n";
}

// The project as a repository with at least one commit: what is needed before it can be sent anywhere.
[[nodiscard]] EditorRepositoryResult EnsureRepository(const std::filesystem::path& projectRoot) {
    std::error_code error;
    if (!std::filesystem::exists(projectRoot / ".git", error)) {
        const ProcessResult init = Run("git", { "init", "-b", "main" }, projectRoot, kQuickTimeoutMs);
        if (!init.Succeeded()) {
            return FailedRun(init, "Git");
        }
    }
    WriteDefaultIgnoreFile(projectRoot);
    if (Run("git", { "rev-parse", "--verify", "HEAD" }, projectRoot, kQuickTimeoutMs).Succeeded()) {
        return { true, {} };
    }
    const ProcessResult added = Run("git", { "add", "-A" }, projectRoot, kSendTimeoutMs);
    if (!added.Succeeded()) {
        return FailedRun(added, "Git");
    }
    const ProcessResult committed = Run("git", { "commit", "--allow-empty", "-m", "Initial commit" }, projectRoot, kSendTimeoutMs);
    return committed.Succeeded() ? EditorRepositoryResult{ true, {} } : FailedRun(committed, "Git");
}

[[nodiscard]] bool LooksLikeRemote(const std::string& url) noexcept {
    return url.starts_with("https://") || url.starts_with("http://") || url.starts_with("ssh://") || url.starts_with("git@");
}

} // namespace

EditorRepositoryState EditorProjectRepository::Query(const std::filesystem::path& projectRoot) {
    EditorRepositoryState state;
    std::error_code error;
    state.hasRepository = std::filesystem::exists(projectRoot / ".git", error);
    if (!state.hasRepository) {
        for (std::filesystem::path probe = projectRoot.parent_path(); !probe.empty() && probe != probe.parent_path(); probe = probe.parent_path()) {
            if (std::filesystem::exists(probe / ".git", error)) {
                state.enclosingRoot = probe;
                break;
            }
        }
        return state;
    }
    if (const ProcessResult branch = Run("git", { "branch", "--show-current" }, projectRoot, kQuickTimeoutMs); branch.Succeeded()) {
        state.branch = Trim(branch.output);
    }
    if (const ProcessResult remote = Run("git", { "remote", "get-url", "origin" }, projectRoot, kQuickTimeoutMs); remote.Succeeded()) {
        state.remoteUrl = Trim(remote.output);
    }
    return state;
}

EditorChangeList EditorProjectRepository::Changes(const std::filesystem::path& projectRoot) {
    EditorChangeList list;
    // No optional locks: this runs in the background and must not block the user's own git commands.
    const ProcessResult top = Run("git", { "--no-optional-locks", "rev-parse", "--show-toplevel" }, projectRoot, kQuickTimeoutMs, false);
    if (!top.Succeeded() || Trim(top.output).empty()) {
        return list;
    }
    const ProcessResult status = Run("git", { "--no-optional-locks", "status", "--porcelain=v2", "-z", "--no-renames", "--untracked-files=all", "--", "." },
        projectRoot, kReachTimeoutMs, false);
    if (!status.Succeeded()) {
        return list;
    }
    list.available = true;
    list.root = std::filesystem::path{ ScriptEditorTextEncoding::Widen(Trim(top.output)) };
    if (const ProcessResult branch = Run("git", { "--no-optional-locks", "branch", "--show-current" }, projectRoot, kQuickTimeoutMs, false); branch.Succeeded()) {
        list.branch = Trim(branch.output);
    }
    // Entries end in NUL. "? path" is untracked; "1 XY <6 more fields> path" changed; "u XY <8 more fields> path" in conflict.
    const auto pathAfterFields = [](const std::string_view entry, int fields) {
        std::size_t at = 0U;
        for (int skipped = 0; skipped < fields; ++skipped) {
            at = entry.find(' ', at);
            if (at == std::string_view::npos) {
                return std::string{};
            }
            ++at;
        }
        return std::string{ entry.substr(at) };
    };
    std::size_t start = 0U;
    while (start < status.output.size()) {
        std::size_t end = status.output.find('\0', start);
        if (end == std::string::npos) {
            end = status.output.size();
        }
        const std::string_view entry{ status.output.data() + start, end - start };
        start = end + 1U;
        EditorFileChange change;
        if (entry.size() > 2U && entry[0] == '?') {
            change.path = std::string{ entry.substr(2U) };
            change.state = EditorChangeState::Untracked;
        } else if (entry.size() > 4U && (entry[0] == '1' || entry[0] == 'u')) {
            change.path = pathAfterFields(entry, entry[0] == '1' ? 8 : 10);
            const char staged = entry[2];
            const char working = entry[3];
            if (entry[0] == '1' && (staged == 'A' || working == 'A')) {
                change.state = EditorChangeState::Added;
            } else if (entry[0] == '1' && (staged == 'D' || working == 'D')) {
                change.state = EditorChangeState::Deleted;
            }
        }
        if (!change.path.empty()) {
            list.changes.push_back(std::move(change));
        }
    }
    std::ranges::sort(list.changes, {}, &EditorFileChange::path);
    return list;
}

std::optional<std::string> EditorProjectRepository::SavedContent(const std::filesystem::path& projectRoot, const std::string& repositoryPath) {
    ProcessResult shown = Run("git", { "--no-optional-locks", "show", "HEAD:" + repositoryPath }, projectRoot, kReachTimeoutMs, false);
    if (!shown.Succeeded()) {
        return std::nullopt;
    }
    return std::move(shown.output);
}

bool EditorProjectRepository::CanRestore(EditorChangeState state) noexcept {
    return state == EditorChangeState::Modified || state == EditorChangeState::Deleted;
}

EditorRepositoryResult EditorProjectRepository::Restore(const std::filesystem::path& projectRoot, const std::string& repositoryPath) {
    // Literal paths: a file named like a pattern is still only that file.
    const ProcessResult restored = Run("git", { "--literal-pathspecs", "checkout", "HEAD", "--", repositoryPath }, projectRoot, kQuickTimeoutMs);
    return restored.Succeeded() ? EditorRepositoryResult{ true, {} } : FailedRun(restored, "Git");
}

bool EditorProjectRepository::GitHubCliAvailable() {
    return Run("gh", { "--version" }, {}, kQuickTimeoutMs).Succeeded();
}

bool EditorProjectRepository::IsValidRepositoryName(const std::string& name) noexcept {
    if (name.empty() || name.size() > 100U || name == "." || name == "..") {
        return false;
    }
    for (const char character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '.' && character != '-' && character != '_') {
            return false;
        }
    }
    return true;
}

std::string EditorProjectRepository::SuggestedRepositoryName(const std::filesystem::path& projectRoot) {
    std::string name = Utf8(projectRoot.filename());
    for (char& character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) == 0 && character != '.' && character != '-' && character != '_') {
            character = '-';
        }
    }
    return name;
}

std::string EditorProjectRepository::BrowserUrl(const std::string& remoteUrl) {
    std::string url = remoteUrl;
    if (url.starts_with("git@")) {
        const std::size_t colon = url.find(':');
        if (colon == std::string::npos) {
            return {};
        }
        url = "https://" + url.substr(4U, colon - 4U) + "/" + url.substr(colon + 1U);
    }
    if (!url.starts_with("https://") && !url.starts_with("http://")) {
        return {};
    }
    if (url.ends_with(".git")) {
        url.resize(url.size() - 4U);
    }
    return url;
}

EditorRepositoryResult EditorProjectRepository::CreateLocal(const std::filesystem::path& projectRoot) {
    EditorRepositoryResult result = EnsureRepository(projectRoot);
    if (result.succeeded) {
        result.message = std::string{ EditorText("repository.localReady", "The local repository is ready, with the project's first commit.") };
    }
    return result;
}

EditorRepositoryResult EditorProjectRepository::ConnectUrl(const std::filesystem::path& projectRoot, const std::string& url) {
    if (!LooksLikeRemote(url)) {
        return Failed(std::string{ EditorText("repository.badAddress", "Enter a repository address starting with https:// or git@.") });
    }
    const ProcessResult reach = Run("git", { "ls-remote", "--heads", url }, projectRoot, kReachTimeoutMs);
    if (!reach.Succeeded()) {
        return FailedRun(reach, "Git");
    }
    if (EditorRepositoryResult ready = EnsureRepository(projectRoot); !ready.succeeded) {
        return ready;
    }
    const bool hasOrigin = Run("git", { "remote", "get-url", "origin" }, projectRoot, kQuickTimeoutMs).Succeeded();
    const ProcessResult set = Run("git", { "remote", hasOrigin ? "set-url" : "add", "origin", url }, projectRoot, kQuickTimeoutMs);
    if (!set.Succeeded()) {
        return FailedRun(set, "Git");
    }
    if (!Trim(reach.output).empty()) {
        return { true, std::string{ EditorText("repository.connectedExisting", "Connected. The repository already has content, so nothing was sent.") } };
    }
    const ProcessResult sent = Run("git", { "push", "-u", "origin", "HEAD" }, projectRoot, kSendTimeoutMs);
    if (!sent.Succeeded()) {
        return FailedRun(sent, "Git");
    }
    return { true, std::string{ EditorText("repository.connectedSent", "Connected, and the project was sent to the repository.") } };
}

EditorRepositoryResult EditorProjectRepository::ConnectGitHub(const std::filesystem::path& projectRoot, const std::string& name) {
    if (!Run("gh", { "auth", "status" }, projectRoot, kQuickTimeoutMs).Succeeded()) {
        // gh's own sign-in, in its own console: the editor never sees the account's credentials.
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring line = L"gh auth login --web --git-protocol https";
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process) == FALSE) {
            return Failed(std::string{ EditorText("repository.signInFailed", "The GitHub sign-in could not be started.") });
        }
        WaitForSingleObject(process.hProcess, kSignInTimeoutMs);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (!Run("gh", { "auth", "status" }, projectRoot, kQuickTimeoutMs).Succeeded()) {
            return Failed(std::string{ EditorText("repository.notSignedIn", "You are not signed in to GitHub, so nothing was created.") });
        }
    }
    if (EditorRepositoryResult ready = EnsureRepository(projectRoot); !ready.succeeded) {
        return ready;
    }
    const ProcessResult created = Run("gh", { "repo", "create", name, "--private", "--source", Utf8(projectRoot), "--remote", "origin", "--push" }, projectRoot, kSendTimeoutMs);
    if (!created.Succeeded()) {
        return FailedRun(created, "GitHub CLI");
    }
    return { true, std::string{ EditorText("repository.gitHubReady", "The project is on GitHub, in a new private repository.") } };
}

} // namespace kb::editor

#endif
