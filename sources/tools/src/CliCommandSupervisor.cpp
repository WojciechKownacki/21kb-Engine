#include "CliCommandSupervisor.hpp"
#include "CliProgram.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace kb::cli {
namespace {

struct Limits {
    std::optional<int> milliseconds;
    std::filesystem::path cancelFile;
};

struct Output {
    std::string out;
    std::string err;
    std::string reason;
    int code = 125;
};

int Emit(const Output& output, std::string_view command, bool json, CommandIo io) {
    if (json && !output.reason.empty()) {
        using kb::core::JsonValue;
        auto result = JsonValue::MakeObject();
        result.Set("schema", JsonValue::MakeString("21kb.cli-result/v1"));
        result.Set("ok", JsonValue::MakeBool(false));
        result.Set("exitCode", JsonValue::MakeNumber(output.code));
        result.Set("command", JsonValue::MakeString(ToValidUtf8(command)));
        result.Set("output", JsonValue::MakeString(ToValidUtf8(output.out)));
        result.Set("reason", JsonValue::MakeString(output.reason));
        auto errors = JsonValue::MakeArray();
        std::istringstream lines{output.err};
        for (std::string line; std::getline(lines, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.starts_with("error: ")) line.erase(0, 7);
            errors.Append(JsonValue::MakeString(ToValidUtf8(line)));
        }
        result.Set("errors", std::move(errors));
        io.out << result.Dump() << '\n';
    } else {
        io.out << output.out;
        io.err << output.err;
    }
    return output.code;
}

#if defined(_WIN32)
std::optional<int> StopRequested(const Limits& limits, std::chrono::steady_clock::time_point started) {
    if (!limits.cancelFile.empty()) {
        std::error_code error;
        const bool requested = std::filesystem::exists(limits.cancelFile, error);
        if (error) throw std::runtime_error("cannot inspect command cancellation file");
        if (requested) return 130;
    }
    if (limits.milliseconds && std::chrono::steady_clock::now() - started >=
            std::chrono::milliseconds{*limits.milliseconds}) return 124;
    return std::nullopt;
}

void Interrupted(Output& output, int code) {
    output.code = code;
    output.reason = code == 124 ? "timeout" : "cancelled";
    output.err += code == 124 ? "command deadline exceeded\n" : "command cancelled by cancellation file\n";
}

class Handle final {
public:
    HANDLE value = nullptr;
    ~Handle() { Reset(); }
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Reset() noexcept {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = nullptr;
    }
};

std::wstring Wide(std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("command argument is too large");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size == 0) throw std::runtime_error("command argument is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size) != size)
        throw std::runtime_error("command argument could not be decoded");
    return result;
}

std::wstring Quote(std::wstring_view value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t item : value) {
        if (item == L'\\') { ++slashes; continue; }
        result.append(item == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result.push_back(item);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

void Pipe(Handle& read, Handle& write) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!CreatePipe(&read.value, &write.value, &security, 0) ||
        !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("command output pipe could not be created");
}

Output Run(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
    const Limits& limits, bool json, CommandIo io) {
    Output output;
    const auto started = std::chrono::steady_clock::now();
    if (const auto stopped = StopRequested(limits, started)) { Interrupted(output, *stopped); return output; }
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimits{};
    jobLimits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &jobLimits, sizeof(jobLimits)))
        throw std::runtime_error("command process job could not be configured");
    Handle outRead, outWrite, errRead, errWrite, input;
    Pipe(outRead, outWrite);
    Pipe(errRead, errWrite);
    const HANDLE standardInput = GetStdHandle(STD_INPUT_HANDLE);
    if (standardInput == nullptr || standardInput == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(), standardInput, GetCurrentProcess(), &input.value, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("command input could not be inherited");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = outWrite.value;
    startup.StartupInfo.hStdError = errWrite.value;
    std::wstring command = Quote(executable.wstring());
    for (const auto& argument : arguments) { command.push_back(L' '); command += Quote(Wide(argument)); }
    SIZE_T bytes = 0;
    static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes));
    std::vector<unsigned char> attributes(bytes);
    startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes))
        throw std::runtime_error("command handle list could not be created");
    std::array inherited{input.value, outWrite.value, errWrite.value};
    const bool ready = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited.data(), sizeof(inherited), nullptr, nullptr) != FALSE;
    PROCESS_INFORMATION process{};
    const bool created = ready && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!created) throw std::runtime_error("command process could not be created");
    Handle child{process.hProcess}, thread{process.hThread};
    if (!AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 125);
        WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("command process could not join its job");
    }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1))
        throw std::runtime_error("command process could not be resumed");
    outWrite.Reset(); errWrite.Reset(); input.Reset(); thread.Reset();
    // Text mode streams as usual. JSON mode buffers so forced interruption
    // still produces exactly one complete result, even during an external build.
    constexpr std::size_t maximumJsonBytes = 16U * 1024U * 1024U;
    const auto drain = [&](HANDLE pipe, std::string& text, std::ostream& stream) {
        DWORD available = 0;
        // Bound work per iteration so a continuously writing child cannot
        // starve deadline checks or the other stream.
        for (unsigned iteration = 0; iteration < 16; ++iteration) {
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) return;
                throw std::runtime_error("command output pipe could not be inspected");
            }
            if (available == 0) return;
            std::array<char, 4096> buffer{};
            DWORD read = 0;
            if (!ReadFile(pipe, buffer.data(), std::min<DWORD>(available, static_cast<DWORD>(buffer.size())), &read, nullptr))
                throw std::runtime_error("command output pipe could not be read");
            if (json) {
                if (output.out.size() + output.err.size() + read > maximumJsonBytes)
                    throw std::runtime_error("supervised JSON output exceeds 16 MiB");
                text.append(buffer.data(), read);
            } else { stream.write(buffer.data(), read); stream.flush(); }
        }
    };
    try {
        for (;;) {
            drain(outRead.value, output.out, io.out);
            drain(errRead.value, output.err, io.err);
            const DWORD wait = WaitForSingleObject(child.value, 10);
            if (wait == WAIT_OBJECT_0) break;
            if (wait != WAIT_TIMEOUT) throw std::runtime_error("command process wait failed");
            if (const auto stopped = StopRequested(limits, started)) {
                Interrupted(output, *stopped);
                TerminateJobObject(job.value, static_cast<UINT>(*stopped));
                WaitForSingleObject(child.value, 5000);
                break;
            }
        }
        if (output.reason.empty()) {
            DWORD code = 125;
            if (!GetExitCodeProcess(child.value, &code) || code > static_cast<DWORD>(std::numeric_limits<int>::max()))
                throw std::runtime_error("command exit code is unavailable");
            output.code = static_cast<int>(code);
        }
        // Stop remaining descendants before draining: no background process may
        // retain the pipes or outlive a completed bounded command.
        if (!TerminateJobObject(job.value, static_cast<UINT>(output.code)))
            throw std::runtime_error("command descendants could not be stopped");
        for (;;) {
            DWORD outBytes = 0, errBytes = 0;
            static_cast<void>(PeekNamedPipe(outRead.value, nullptr, 0, nullptr, &outBytes, nullptr));
            static_cast<void>(PeekNamedPipe(errRead.value, nullptr, 0, nullptr, &errBytes, nullptr));
            if (!outBytes && !errBytes) break;
            drain(outRead.value, output.out, io.out);
            drain(errRead.value, output.err, io.err);
        }
    } catch (const std::exception& error) {
        TerminateJobObject(job.value, 125);
        WaitForSingleObject(child.value, 5000);
        output.code = 125;
        output.reason = "supervisor-error";
        output.err += std::string{error.what()} + '\n';
    }
    return output;
}
#endif

} // namespace

std::optional<int> SuperviseCliCommand(const std::filesystem::path& executable,
    std::span<const std::string> arguments, CommandIo io) {
    Limits limits;
    std::vector<std::string> child;
    std::string command;
    std::string error;
    bool requested = false, prefix = true, json = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& token = arguments[i];
        if (prefix && (token == "--command-timeout-ms" || token == "--command-cancel-file")) {
            requested = true;
            if (i + 1 == arguments.size()) { error = "missing value for " + token; break; }
            const auto& value = arguments[++i];
            if (token == "--command-timeout-ms") {
                int milliseconds = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), milliseconds);
                if (limits.milliseconds || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    milliseconds < 1 || milliseconds > 86400000) error = "--command-timeout-ms expects one value in 1..86400000";
                else limits.milliseconds = milliseconds;
            } else {
                if (!limits.cancelFile.empty() || value.empty()) error = "--command-cancel-file expects one nonempty path";
                else limits.cancelFile = kb::assets::Utf8Path(value);
            }
            continue;
        }
        if (token == "--json") json = true;
        else if (prefix) { command = token; prefix = false; }
        child.push_back(token);
    }
    if (!requested) return std::nullopt;
    if (command == "mcp") json = false;
    if (command.empty() && error.empty()) error = "command limits require a command";
    Output output;
    try {
        if (!error.empty()) throw std::runtime_error(error);
#if defined(_WIN32)
        output = Run(executable, child, limits, json, io);
#else
        static_cast<void>(executable);
        throw std::runtime_error("command process limits are currently supported on Windows");
#endif
    } catch (const std::exception& failure) {
        output.err = std::string{failure.what()} + '\n';
        output.reason = "supervisor-error";
    }
    return Emit(output, command, json, io);
}

} // namespace kb::cli
