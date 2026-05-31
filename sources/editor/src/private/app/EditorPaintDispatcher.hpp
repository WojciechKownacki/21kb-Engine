#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPointerDragState.hpp"
#include "rendering/EditorGdiRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorPaintDispatcher {
public:
#if defined(_WIN32)
    EditorPaintDispatcher(
        HWND& mainWindow,
        EditorDockModel& dockModel,
        EditorSceneContext& sceneContext,
        EditorTheme& theme,
        EditorMetrics& metrics,
        EditorGdiRenderer& renderer,
        EditorFloatingWindowManager& floatingWindows,
        EditorDockController& dockController,
        EditorPointerDragState& pointerDrag) noexcept;

    void Paint(HWND paintWindow) const;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool IsMainWindow(HWND candidate) const noexcept;

    HWND& mainWindow_;
    EditorDockModel& dockModel_;
    EditorSceneContext& sceneContext_;
    EditorTheme& theme_;
    EditorMetrics& metrics_;
    EditorGdiRenderer& renderer_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorPointerDragState& pointerDrag_;
#endif
};

} // namespace kb::editor
