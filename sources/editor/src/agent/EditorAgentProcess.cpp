#include "agent/EditorAgentProcess.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cwctype>

namespace kb::editor {
namespace {

// One argument quoted the way CommandLineToArgvW reads it back.
void AppendQuoted(std::wstring& line, const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += argument;
        return;
    }
    line += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            line.append(backslashes * 2 + 1, L'\\');
        } else {
            line.append(backslashes, L'\\');
        }
        backslashes = 0;
        line += character;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
}

[[nodiscard]] std::wstring Lowercase(std::wstring text) {
    std::ranges::transform(text, text.begin(), [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return text;
}

// The current environment with `overrides` set, as the block CreateProcessW takes.
[[nodiscard]] std::wstring EnvironmentBlock(const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {
    std::vector<std::wstring> entries;
    if (wchar_t* strings = GetEnvironmentStringsW(); strings != nullptr) {
        for (const wchar_t* entry = strings; *entry != L'\0'; entry += wcslen(entry) + 1) {
            entries.emplace_back(entry);
        }
        FreeEnvironmentStringsW(strings);
    }
    for (const auto& [name, value] : overrides) {
        const std::wstring key = Lowercase(name) + L"=";
        std::erase_if(entries, [&key](const std::wstring& entry) { return Lowercase(entry.substr(0, key.size())) == key; });
        entries.push_back(name + L"=" + value);
    }
    std::wstring block;
    for (const std::wstring& entry : entries) {
        block += entry;
        block += L'\0';
    }
    block += L'\0';
    return block;
}

} // namespace

EditorAgentProcess::~EditorAgentProcess() {
    Stop();
    Join();
}

bool EditorAgentProcess::Start(
    const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments,
    const std::filesystem::path& workingDirectory,
    const std::vector<std::pair<std::wstring, std::wstring>>& environment,
    std::string input,
    std::string& error) {
    if (process_ != nullptr && !finished_) {
        error = "the agent is still working";
        return false;
    }
    Join();
    {
        const std::lock_guard lock(mutex_);
        lines_.clear();
    }
    finished_ = false;
    exitCode_ = 0;

    std::wstring line;
    const std::wstring extension = Lowercase(executable.extension().wstring());
    const bool script = extension == L".cmd" || extension == L".bat";
    if (script) {
        // A script runs through the command processor; /s keeps the quotes of the inner line.
        wchar_t comspec[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        AppendQuoted(line, length > 0U && length < MAX_PATH ? std::wstring{ comspec } : std::wstring{ L"cmd.exe" });
        line += L" /d /s /c \"";
    }
    AppendQuoted(line, executable.wstring());
    for (const std::wstring& argument : arguments) {
        line += L' ';
        AppendQuoted(line, argument);
    }
    if (script) {
        line += L'"';
    }

    SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    if (CreatePipe(&outputRead, &outputWrite, &inherit, 0) == 0 || CreatePipe(&inputRead, &inputWrite, &inherit, 0) == 0) {
        error = "the agent's pipes could not be created";
        for (HANDLE handle : { outputRead, outputWrite, inputRead, inputWrite }) {
            if (handle != nullptr) CloseHandle(handle);
        }
        return false;
    }
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    PROCESS_INFORMATION info{};
    std::wstring environmentBlock = EnvironmentBlock(environment);
    const std::wstring folder = workingDirectory.wstring();
    const BOOL created = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, environmentBlock.data(),
        folder.empty() ? nullptr : folder.c_str(), &startup, &info);
    CloseHandle(outputWrite);
    CloseHandle(inputRead);
    if (created == 0) {
        error = "the agent could not be started (error " + std::to_string(GetLastError()) + ")";
        CloseHandle(outputRead);
        CloseHandle(inputWrite);
        return false;
    }

    // Everything the agent starts lives in this job, so Stop and closing the editor end it all.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, info.hProcess);
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    job_ = job;

    reader_ = std::thread([this, outputRead, inputWrite, input = std::move(input)]() {
        DWORD written = 0;
        if (!input.empty()) {
            WriteFile(inputWrite, input.data(), static_cast<DWORD>(input.size()), &written, nullptr);
        }
        CloseHandle(inputWrite);
        std::string pending;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(outputRead, buffer, sizeof(buffer), &read, nullptr) != 0 && read > 0U) {
            pending.append(buffer, read);
            std::size_t end = 0;
            while ((end = pending.find('\n')) != std::string::npos) {
                std::string text = pending.substr(0, end);
                if (!text.empty() && text.back() == '\r') text.pop_back();
                pending.erase(0, end + 1);
                const std::lock_guard lock(mutex_);
                lines_.push_back(std::move(text));
            }
        }
        if (!pending.empty()) {
            const std::lock_guard lock(mutex_);
            lines_.push_back(std::move(pending));
        }
        CloseHandle(outputRead);
        WaitForSingleObject(static_cast<HANDLE>(process_), INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
        exitCode_ = static_cast<int>(code);
        finished_ = true;
    });
    return true;
}

std::vector<std::string> EditorAgentProcess::TakeLines() {
    const std::lock_guard lock(mutex_);
    std::vector<std::string> lines(std::make_move_iterator(lines_.begin()), std::make_move_iterator(lines_.end()));
    lines_.clear();
    return lines;
}

bool EditorAgentProcess::Busy() const noexcept {
    if (process_ == nullptr) {
        return false;
    }
    const std::lock_guard lock(mutex_);
    return !finished_ || !lines_.empty();
}

std::optional<int> EditorAgentProcess::ExitCode() const noexcept {
    if (process_ == nullptr || Busy()) {
        return std::nullopt;
    }
    return exitCode_.load();
}

void EditorAgentProcess::Stop() noexcept {
    if (job_ != nullptr && !finished_) {
        TerminateJobObject(static_cast<HANDLE>(job_), 1U);
    } else if (process_ != nullptr && !finished_) {
        TerminateProcess(static_cast<HANDLE>(process_), 1U);
    }
}

void EditorAgentProcess::Join() noexcept {
    if (reader_.joinable()) {
        reader_.join();
    }
    if (process_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
    if (job_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
    }
}

} // namespace kb::editor

#endif
