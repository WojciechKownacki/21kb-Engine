#include "app/EditorCrashBreadcrumbs.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kb::editor {
namespace {

std::mutex g_breadcrumbMutex;

[[nodiscard]] std::filesystem::path BreadcrumbPath() {
    return std::filesystem::current_path() / "Saved" / "Logs" / "editor-crash-breadcrumbs.log";
}

[[nodiscard]] std::string NowMs() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(millis);
}

[[nodiscard]] std::uint64_t CurrentThreadIdValue() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentThreadId());
#else
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

// EditorCrashBreadcrumbs::Write is called unconditionally from ~150+ sites across the per-frame
// paint/submit path (it exists to leave a trail leading up to a crash, so it can't just be gated
// off like the opt-in renderer debug traces). The previous implementation opened, wrote, and closed
// a file handle -- plus re-checked/created its parent directory -- on every single call, which meant
// every rendered frame paid for dozens of synchronous filesystem round trips regardless of scene
// complexity. Keeping the handles open for the process lifetime and only flush()-ing (which just
// pushes the already-buffered bytes out, not a full open/close) keeps the same crash-time durability
// at a fraction of the cost.
[[nodiscard]] std::ofstream& BreadcrumbStream() {
    static std::ofstream stream = [] {
        std::error_code error;
        std::filesystem::create_directories(BreadcrumbPath().parent_path(), error);
        return std::ofstream{BreadcrumbPath(), std::ios::out | std::ios::app};
    }();
    return stream;
}

void AppendLine(std::string_view line) {
    // Single mutex covers both persistent stream handles below -- they're shared across calls (and
    // threads) now instead of each call getting its own throwaway ofstream, so every read/write on
    // them has to be serialized here, not just the main breadcrumb log's.
    std::lock_guard lock{g_breadcrumbMutex};
    std::ofstream& output = BreadcrumbStream();
    if (!output.is_open()) {
        return;
    }
    output << line << '\n';
    output.flush();
}

#if defined(_WIN32)
LONG WINAPI BreadcrumbUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) {
    std::ostringstream line;
    line << NowMs()
         << " tid=" << CurrentThreadIdValue()
         << " [crash] unhandled_exception";
    if (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr) {
        const auto address = reinterpret_cast<std::uintptr_t>(exceptionPointers->ExceptionRecord->ExceptionAddress);
        line << " code=0x" << std::hex << exceptionPointers->ExceptionRecord->ExceptionCode
             << " address=0x" << address;
        if (const HMODULE module = GetModuleHandleW(nullptr); module != nullptr) {
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            line << " module_base=0x" << base;
            if (address >= base) {
                line << " rva=0x" << (address - base);
            }
        }
    }
    AppendLine(line.str());
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

void EditorCrashBreadcrumbs::Reset() {
    std::lock_guard lock{g_breadcrumbMutex};
    std::error_code error;
    std::filesystem::create_directories(BreadcrumbPath().parent_path(), error);
    std::ofstream output{BreadcrumbPath(), std::ios::out | std::ios::trunc};
    if (!output.is_open()) {
        return;
    }
    output << NowMs() << " tid=" << CurrentThreadIdValue() << " [app] breadcrumb_log_reset\n";
    output.flush();
}

void EditorCrashBreadcrumbs::Write(std::string_view category, std::string_view message) {
    std::ostringstream line;
    line << NowMs()
         << " tid=" << CurrentThreadIdValue()
         << " [" << category << "] " << message;
    AppendLine(line.str());
}

void EditorCrashBreadcrumbs::WriteValue(std::string_view category, std::string_view label, std::uint64_t value) {
    std::ostringstream line;
    line << label << '=' << value;
    Write(category, line.str());
}

void EditorCrashBreadcrumbs::InstallUnhandledExceptionLogger() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(BreadcrumbUnhandledExceptionFilter);
#endif
    Write("app", "unhandled_exception_logger_installed");
}

} // namespace kb::editor
