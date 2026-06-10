#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "engine/assets/AssetId.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Drops a behaviour (script) asset onto the entity under the cursor in the Scene
// viewport, attaching the script to it. Lets users drag a .lua straight onto the
// object they see, instead of only onto its Hierarchy row.
class EditorBehaviourAssetSceneDropHandler {
public:
    EditorBehaviourAssetSceneDropHandler() = delete;

    [[nodiscard]] static bool Drop(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId);
};

#endif

} // namespace kb::editor
