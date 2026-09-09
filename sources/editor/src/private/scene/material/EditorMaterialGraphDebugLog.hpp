#pragma once

#include <filesystem>
#include <mutex>
#include <string_view>

namespace kb::editor {

class EditorConsoleState;

// Material graph debug tracing shared by EditorSceneContext.cpp and
// EditorSceneContextMaterialGraph.cpp. The log mutex and log path own
// process-wide state, so they are defined once in EditorMaterialGraphDebugLog.cpp.
std::filesystem::path MaterialGraphDebugLogPath(std::string_view extension);
std::mutex& MaterialGraphDebugLogMutex();
bool MaterialGraphDebugLoggingEnabled() noexcept;
void WriteMaterialGraphDebugTrace(std::string_view message);
void LogMaterialGraphDebug(EditorConsoleState& console, std::string_view message);

} // namespace kb::editor
