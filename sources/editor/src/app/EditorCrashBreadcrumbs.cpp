#include "app/EditorCrashBreadcrumbs.hpp"

#include <chrono>
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

void AppendLine(std::string_view line) {
    std::lock_guard lock{g_breadcrumbMutex};
    std::error_code error;
    std::filesystem::create_directories(BreadcrumbPath().parent_path(), error);
    std::ofstream output{BreadcrumbPath(), std::ios::out | std::ios::app};
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
        line << " code=0x" << std::hex << exceptionPointers->ExceptionRecord->ExceptionCode
             << " address=0x" << reinterpret_cast<std::uintptr_t>(exceptionPointers->ExceptionRecord->ExceptionAddress);
    }
    AppendLine(line.str());
    return EXCEPTION_EXECUTE_HANDLER;
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
