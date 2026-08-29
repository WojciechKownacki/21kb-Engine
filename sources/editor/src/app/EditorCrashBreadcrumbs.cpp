#include "app/EditorCrashBreadcrumbs.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
std::string g_apiVersion = "unknown";
std::string g_apiHash = "unknown";
std::vector<std::pair<std::uint64_t, std::string>> g_assets;
std::vector<std::string> g_recentCategories;

[[nodiscard]] std::filesystem::path BreadcrumbPath() {
    return std::filesystem::current_path() / "Saved" / "Logs" / "editor-crash-breadcrumbs.log";
}

// Where the previous run's trail is kept. A crash is usually reported after the editor
// has been started again, and truncating on start threw away the one thing worth reading:
// the last step before the process died.
[[nodiscard]] std::filesystem::path PreviousBreadcrumbPath() {
    return std::filesystem::current_path() / "Saved" / "Logs" / "editor-crash-breadcrumbs.prev.log";
}

[[nodiscard]] std::filesystem::path CrashReportPath() {
    return std::filesystem::current_path() / "Saved" / "Crashes" / "editor-crash-report.json";
}

[[nodiscard]] std::string JsonEscape(std::string_view value) {
    std::string escaped;
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20U) {
                escaped += "\\u00";
                escaped.push_back(hex[(character >> 4U) & 0x0fU]);
                escaped.push_back(hex[character & 0x0fU]);
            } else {
                escaped.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return escaped;
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

void AppendCategory(std::string_view category) {
    std::lock_guard lock{g_breadcrumbMutex};
    if (g_recentCategories.size() == 32U) g_recentCategories.erase(g_recentCategories.begin());
    g_recentCategories.emplace_back(category);
}

#if defined(_WIN32)
[[nodiscard]] std::string SehErrorKind(
    const EXCEPTION_POINTERS* exceptionPointers) {
    if (exceptionPointers == nullptr ||
        exceptionPointers->ExceptionRecord == nullptr) {
        return "unhandled_exception";
    }
    std::ostringstream error;
    error << "seh_0x" << std::hex
          << exceptionPointers->ExceptionRecord->ExceptionCode;
    return error.str();
}

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
    EditorCrashBreadcrumbs::WriteCrashReport(SehErrorKind(exceptionPointers));
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

[[noreturn]] void BreadcrumbTerminateHandler() noexcept {
    EditorCrashBreadcrumbs::WriteCrashReport("std_terminate");
    std::abort();
}

} // namespace

void EditorCrashBreadcrumbs::Reset() {
    std::lock_guard lock{g_breadcrumbMutex};
    g_recentCategories.clear();
    std::error_code error;
    std::filesystem::create_directories(BreadcrumbPath().parent_path(), error);
    // Keep the run that just ended before starting a new trail over it. Only one is kept:
    // the interesting session is the one that died, and it is the one that just finished.
    if (std::filesystem::exists(BreadcrumbPath(), error)) {
        std::filesystem::remove(PreviousBreadcrumbPath(), error);
        std::filesystem::rename(BreadcrumbPath(), PreviousBreadcrumbPath(), error);
    }
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
    AppendCategory(category);
}

void EditorCrashBreadcrumbs::ConfigureCrashReport(
    std::string apiVersion,
    std::string apiHash,
    std::vector<std::pair<std::uint64_t, std::string>> assets) {
    std::lock_guard lock{g_breadcrumbMutex};
    g_apiVersion = std::move(apiVersion);
    g_apiHash = std::move(apiHash);
    g_assets = std::move(assets);
    if (g_assets.size() > 128U) g_assets.resize(128U);
}

void EditorCrashBreadcrumbs::WriteCrashReport(std::string_view errorKind) noexcept {
    try {
        std::lock_guard lock{g_breadcrumbMutex};
        std::error_code error;
        std::filesystem::create_directories(CrashReportPath().parent_path(), error);
        std::ofstream output{CrashReportPath(), std::ios::out | std::ios::trunc};
        if (!output) return;
        output << "{\"schema\":\"21kb.crash-report/v1\",\"error\":\"" << JsonEscape(errorKind)
               << "\",\"api\":{\"version\":\"" << JsonEscape(g_apiVersion) << "\",\"hash\":\"" << JsonEscape(g_apiHash) << "\"},\"assets\":[";
        for (std::size_t index = 0; index < g_assets.size(); ++index) {
            if (index != 0U) output << ',';
            output << "{\"id\":" << g_assets[index].first << ",\"type\":\"" << JsonEscape(g_assets[index].second) << "\"}";
        }
        output << "],\"recentEvents\":[";
        for (std::size_t index = 0; index < g_recentCategories.size(); ++index) {
            if (index != 0U) output << ',';
            output << "\"" << JsonEscape(g_recentCategories[index]) << "\"";
        }
        output << "]}\n";
    } catch (...) {
    }
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
    std::set_terminate(BreadcrumbTerminateHandler);
    Write("app", "unhandled_exception_logger_installed");
}

} // namespace kb::editor
