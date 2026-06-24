#pragma once

#include "docking/EditorDockModel.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <vector>

namespace kb::editor {

class EditorHostSurfaceLayoutResolver {
public:
    EditorHostSurfaceLayoutResolver() = delete;

#if defined(_WIN32)
    [[nodiscard]] static std::vector<EditorSceneBgfxViewport::HostSurfaceLayout> ResolveMainWindow(
        HWND window,
        const EditorDockModel& dockModel,
        const EditorMetrics& metrics,
        const EditorSceneContext& sceneContext);

    static void SyncMainWindow(
        HWND window,
        const EditorDockModel& dockModel,
        const EditorMetrics& metrics,
        const EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport);
#endif
};

} // namespace kb::editor
