#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
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

namespace kb::render {

inline bool RendererDebugLogEnabled(std::string_view category) noexcept {
    if (category == "aa_trace") {
        return true;
    }
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&buffer, &size, "KB_RENDERER_BREADCRUMBS") != 0 || buffer == nullptr) {
        return false;
    }
    const std::string value{ buffer };
    std::free(buffer);
    return value == "1" || value == "true" || value == "TRUE";
#else
    const char* value = std::getenv("KB_RENDERER_BREADCRUMBS");
    if (value == nullptr) {
        return false;
    }
    const std::string_view text{ value };
    return text == "1" || text == "true" || text == "TRUE";
#endif
}

inline std::filesystem::path RendererDebugLogPath() {
    return std::filesystem::current_path() / "Saved" / "Logs" / "editor-crash-breadcrumbs.log";
}

inline std::filesystem::path RendererAaTraceLogPath() {
    return std::filesystem::current_path() / "aa_trace.log";
}

inline std::string RendererDebugLogNowMs() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(millis);
}

inline std::uint64_t RendererDebugLogThreadId() noexcept {
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

inline void WriteRendererDebugLog(std::string_view category, std::string_view message) {
    if (!RendererDebugLogEnabled(category)) {
        return;
    }

    try {
        std::ostringstream line;
        line << RendererDebugLogNowMs()
             << " tid=" << RendererDebugLogThreadId()
             << " [" << category << "] " << message;
#if defined(_WIN32)
        if (category == "aa_trace") {
            std::string debugLine = line.str();
            debugLine.push_back('\n');
            OutputDebugStringA(debugLine.c_str());
            static bool consoleAttachAttempted = false;
            if (!consoleAttachAttempted) {
                consoleAttachAttempted = true;
                if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                    FILE* stream = nullptr;
                    static_cast<void>(freopen_s(&stream, "CONOUT$", "a", stderr));
                }
            }
            std::fputs(debugLine.c_str(), stderr);
            std::ofstream aaTraceOutput{ RendererAaTraceLogPath(), std::ios::out | std::ios::app };
            if (aaTraceOutput.is_open()) {
                aaTraceOutput << line.str() << '\n';
            }
        }
#endif
        static std::mutex mutex;
        std::lock_guard lock{ mutex };
        const std::filesystem::path path = RendererDebugLogPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output{ path, std::ios::out | std::ios::app };
        if (!output.is_open()) {
            return;
        }
        output << line.str() << '\n';
    } catch (...) {
    }
}

} // namespace kb::render
