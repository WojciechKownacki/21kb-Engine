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
#include <unordered_map>

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
    // aa_trace/grid_trace/mesh_taa_trace used to be hardcoded to always-on debug instrumentation
    // left over from diagnosing specific AA/grid bugs. With ~100 call sites in Renderer.cpp alone,
    // firing unconditionally on every scene submission meant every frame -- even for an empty scene
    // with nothing to render -- paid for dozens of synchronous file opens/writes/closes, dominating
    // frame time far more than any GPU work. All categories are now opt-in via KB_RENDERER_BREADCRUMBS
    // so a normal run does zero logging I/O; the env lookup itself is cached, not repeated per call.
    static_cast<void>(category);
    static const bool enabled = [] {
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
    }();
    return enabled;
}

inline std::filesystem::path RendererDebugLogPath() {
    return std::filesystem::current_path() / "Saved" / "Logs" / "editor-crash-breadcrumbs.log";
}

inline std::filesystem::path RendererAaTraceLogPath() {
    return std::filesystem::current_path() / "aa_trace.log";
}

inline std::filesystem::path RendererGridTraceLogPath() {
    return std::filesystem::current_path() / "grid_trace.log";
}

inline std::filesystem::path RendererMeshTaaTraceLogPath() {
    return std::filesystem::current_path() / "mesh_taa_trace.log";
}

inline std::filesystem::path RendererMaterialGraphDebugLogPath(std::string_view extension) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    return (error ? std::filesystem::current_path() : root) / ("_material_graph_debug" + std::string{ extension });
}

inline std::string RendererDebugLogNowMs() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(millis);
}

inline std::uint64_t RendererDebugLogThreadId() noexcept {
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

// Keeps one file handle open per log path for the process lifetime instead of the previous
// open+write+close-on-every-call pattern (each call also re-checked/created the parent directory).
// A repeated open/close cycle is a full filesystem round trip; a flush() on an already-open handle
// only has to push the buffered bytes out, which is what actually matters for crash-time durability.
inline std::mutex& RendererDebugLogMutex() {
    static std::mutex mutex;
    return mutex;
}

// Callers must hold RendererDebugLogMutex() for as long as they read/write the returned stream --
// it's a shared, persistent handle now, not a fresh object per call.
inline std::ofstream& RendererDebugLogStreamFor(const std::filesystem::path& path) {
    static std::unordered_map<std::string, std::ofstream> streams;
    const std::string key = path.string();
    auto it = streams.find(key);
    if (it == streams.end()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        it = streams.emplace(key, std::ofstream{ path, std::ios::out | std::ios::app }).first;
    }
    return it->second;
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

        std::lock_guard lock{ RendererDebugLogMutex() };
        if (category == "aa_trace" || category == "grid_trace" || category == "mesh_taa_trace") {
            const std::filesystem::path tracePath = category == "grid_trace"
                ? RendererGridTraceLogPath()
                : (category == "mesh_taa_trace" ? RendererMeshTaaTraceLogPath() : RendererAaTraceLogPath());
            std::ofstream& traceOutput = RendererDebugLogStreamFor(tracePath);
            if (traceOutput.is_open()) {
                traceOutput << line.str() << '\n';
                traceOutput.flush();
            }
#if defined(_WIN32)
            std::string debugLine = line.str();
            debugLine.push_back('\n');
            OutputDebugStringA(debugLine.c_str());
#endif
        }
        std::ofstream& output = RendererDebugLogStreamFor(RendererDebugLogPath());
        if (!output.is_open()) {
            return;
        }
        output << line.str() << '\n';
        output.flush();
    } catch (...) {
    }
}

inline void WriteRendererMaterialGraphDebugLog(std::string_view category, std::string_view message) {
    try {
        std::ostringstream line;
        line << RendererDebugLogNowMs()
             << " tid=" << RendererDebugLogThreadId()
             << " [RendererMaterialGraph/" << category << "] " << message;

        std::lock_guard lock{ RendererDebugLogMutex() };
        const std::string text = line.str();
        for (std::string_view extension : { std::string_view{ ".log" }, std::string_view{ ".md" } }) {
            std::ofstream& output = RendererDebugLogStreamFor(RendererMaterialGraphDebugLogPath(extension));
            if (output.is_open()) {
                output << text << '\n';
                output.flush();
            }
        }
#if defined(_WIN32)
        std::string debugLine = text;
        debugLine.push_back('\n');
        OutputDebugStringA(debugLine.c_str());
#endif
        std::fprintf(stderr, "%s\n", text.c_str());
        std::fflush(stderr);
    } catch (...) {
    }
}

} // namespace kb::render
