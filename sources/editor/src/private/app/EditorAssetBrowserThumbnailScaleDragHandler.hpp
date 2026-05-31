#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorSceneContext;

class EditorAssetBrowserThumbnailScaleDragHandler {
public:
    EditorAssetBrowserThumbnailScaleDragHandler() = delete;

    [[nodiscard]] static bool HandlePointerMove(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool HandlePointerUp(EditorSceneContext& sceneContext) noexcept;
};

#endif

} // namespace kb::editor
