#pragma once

#include "app/EditorShellInteractionState.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <filesystem>
#include <vector>

namespace kb::editor {

#if defined(_WIN32)

// The Project menu's commands. The editor holds one project per process, so going to another
// is a normal close, with its questions about unsaved work, and a fresh start on that project.
class EditorProjectCommands {
public:
    EditorProjectCommands() = delete;

    [[nodiscard]] static std::vector<EditorPathMenuRow> RecentProjectRows();
    static void RememberCurrentProject();
    static void OpenProject(HWND owner, EditorSceneContext& sceneContext);
    static void NewProject(HWND owner, EditorSceneContext& sceneContext);
    static void SwitchTo(HWND owner, EditorSceneContext& sceneContext, const std::filesystem::path& projectFile);
    static void ShowInExplorer(HWND owner, EditorSceneContext& sceneContext);

    // The close that was to end in another project was cancelled.
    static void AbandonPendingSwitch() noexcept;
    // Called last on the way out: starts the editor on the project asked for, if any.
    static void LaunchPendingSwitch();
};

#endif

} // namespace kb::editor
