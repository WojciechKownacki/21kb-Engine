#pragma once

#include "app/EditorPointerDragState.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorPointerDragInteraction {
public:
    EditorPointerDragInteraction() = delete;

#if defined(_WIN32)
    static void CaptureIfActive(HWND messageWindow, const EditorPointerDragState& drag) noexcept;
    [[nodiscard]] static bool Move(HWND sourceWindow, HWND mainWindow, int x, int y, EditorPointerDragState& drag) noexcept;
    [[nodiscard]] static bool Complete(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& drag);
    [[nodiscard]] static bool UpdateCursor(const EditorPointerDragState& drag) noexcept;
#endif
};

} // namespace kb::editor
