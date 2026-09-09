#include "scene/material/EditorMaterialGraphDebugLog.hpp"

#include "console/EditorConsoleState.hpp"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

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

std::filesystem::path MaterialGraphDebugLogPath(std::string_view extension) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    return (error ? std::filesystem::current_path() : root) / ("_material_graph_debug" + std::string{ extension });
}

std::mutex& MaterialGraphDebugLogMutex() {
    static std::mutex mutex;
    return mutex;
}

bool MaterialGraphDebugLoggingEnabled() noexcept {
    return false;
}

void WriteMaterialGraphDebugTrace(std::string_view message) {
    if (!MaterialGraphDebugLoggingEnabled()) {
        return;
    }
    try {
        std::ostringstream line;
        line << "[MaterialGraph] " << message;
        const std::string text = line.str();

        std::lock_guard lock{ MaterialGraphDebugLogMutex() };
        for (std::string_view extension : { std::string_view{ ".log" }, std::string_view{ ".md" } }) {
            std::ofstream output{ MaterialGraphDebugLogPath(extension), std::ios::out | std::ios::app };
            if (output.is_open()) {
                output << text << '\n';
            }
        }
#if defined(_WIN32)
        std::string debugLine = text;
        debugLine.push_back('\n');
        OutputDebugStringA(debugLine.c_str());
#endif
    } catch (...) {
    }
}

void LogMaterialGraphDebug(EditorConsoleState& console, std::string_view message) {
    if (!MaterialGraphDebugLoggingEnabled()) {
        return;
    }
    WriteMaterialGraphDebugTrace(message);
    console.Info("MaterialGraph", std::string{ message });
}

} // namespace kb::editor
