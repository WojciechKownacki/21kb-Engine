#pragma once

#include "app/scene_viewport/EditorSceneViewportTypes.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

struct EditorMetrics;

#if defined(_WIN32)

class EditorSceneViewportHitResolver {
public:
    EditorSceneViewportHitResolver() = delete;

    [[nodiscard]] static std::optional<EditorSceneViewportHit> ResolveRay(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static std::optional<EditorSceneViewportHit> ResolveGroundHit(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
