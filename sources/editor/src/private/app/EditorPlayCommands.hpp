#pragma once

#include "app/EditorPlayModeState.hpp"
#include "docking/EditorDockModel.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// What the Play button and Ctrl+P do, so both follow the options under the arrow beside Play:
// the scene in the Game View (opened beside the Scene View, with the keyboard and mouse going to the game),
// in the Scene View, the Scene View filling the editor while the game runs, or the project in the game's own
// program from its startup scene.
class EditorPlayCommands {
public:
    EditorPlayCommands() = delete;

    // True when play mode began; running the game in its own program leaves the editor stopped.
    [[nodiscard]] static bool Start(HWND owner, EditorDockModel& dockModel, EditorSceneContext& sceneContext, EditorPlayModeState& playMode);
    // Puts the workspace back after play mode, however it ended.
    static void RestoreWorkspace(EditorDockModel& dockModel) noexcept;
};

#endif

} // namespace kb::editor
