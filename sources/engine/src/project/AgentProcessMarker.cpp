#include "engine/project/AgentProcessMarker.hpp"

#include "engine/core/JsonValue.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#include <cerrno>
#include <fstream>
#include <iterator>
#include <system_error>

namespace kb::project {
namespace {

namespace fs = std::filesystem;
using kb::core::JsonValue;

#if defined(_WIN32)
[[nodiscard]] std::uint64_t StartMilliseconds(HANDLE process) {
    FILETIME created{};
    FILETIME ignored{};
    if (GetProcessTimes(process, &created, &ignored, &ignored, &ignored) == FALSE) return 0U;
    const std::uint64_t ticks = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
    return ticks / 10000U;
}

[[nodiscard]] std::uint64_t CurrentPid() { return GetCurrentProcessId(); }

[[nodiscard]] std::uint64_t CurrentStart() { return StartMilliseconds(GetCurrentProcess()); }

[[nodiscard]] bool Runs(std::uint64_t pid, std::uint64_t started) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT && StartMilliseconds(process) == started;
    CloseHandle(process);
    return running;
}
#else
// Other hosts know the pid only: a reused pid is taken for the writer.
[[nodiscard]] std::uint64_t CurrentPid() { return static_cast<std::uint64_t>(getpid()); }

[[nodiscard]] std::uint64_t CurrentStart() { return 0U; }

[[nodiscard]] bool Runs(std::uint64_t pid, std::uint64_t) {
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}
#endif

} // namespace

bool WriteAgentProcessMarker(const fs::path& file, std::string_view detail, std::string& error) {
    std::error_code code;
    fs::create_directories(file.parent_path(), code);
    JsonValue document = JsonValue::MakeObject();
    document.Set("pid", JsonValue::MakeNumber(static_cast<double>(CurrentPid())));
    document.Set("started", JsonValue::MakeNumber(static_cast<double>(CurrentStart())));
    document.Set("detail", JsonValue::MakeString(std::string{ detail }));
    const fs::path temporary = fs::path{ file }.concat(".tmp");
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output << document.Dump();
        if (!output.good()) {
            error = "cannot write " + file.generic_string();
            return false;
        }
    }
    fs::rename(temporary, file, code);
    if (code) {
        error = "cannot write " + file.generic_string() + ": " + code.message();
        return false;
    }
    return true;
}

AgentProcessMarker ReadAgentProcessMarker(const fs::path& file) {
    AgentProcessMarker marker;
    std::error_code code;
    if (!fs::exists(file, code)) return marker;
    marker.state = AgentMarkerState::Damaged;
    std::ifstream input{ file, std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    JsonValue document;
    std::string parseError;
    if (!JsonValue::Parse(text, document, parseError)) {
        marker.error = "not JSON: " + parseError;
        return marker;
    }
    const JsonValue* pid = document.Find("pid");
    const JsonValue* started = document.Find("started");
    const JsonValue* detail = document.Find("detail");
    if (pid == nullptr || started == nullptr || detail == nullptr || pid->GetKind() != JsonValue::Kind::Number ||
        started->GetKind() != JsonValue::Kind::Number || detail->GetKind() != JsonValue::Kind::String || pid->AsNumber() < 1.0) {
        marker.error = "it lacks a pid, a start time or a detail";
        return marker;
    }
    marker.pid = static_cast<std::uint64_t>(pid->AsNumber());
    marker.started = static_cast<std::uint64_t>(started->AsNumber());
    marker.detail = detail->AsString();
    marker.state = Runs(marker.pid, marker.started) ? AgentMarkerState::Alive : AgentMarkerState::Dead;
    return marker;
}

} // namespace kb::project
