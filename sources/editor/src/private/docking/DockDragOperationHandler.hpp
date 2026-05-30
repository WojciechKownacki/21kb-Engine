#pragma once

#include "docking/DockPointerDrag.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class DockDragOperationHandler {
public:
    DockDragOperationHandler() = delete;

#if defined(_WIN32)
    static void Move(
        DockPointerDrag& drag,
        HWND eventWindow,
        int x,
        int y,
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        std::optional<DockDropPreview>& dropPreview);

    static void Complete(
        const DockPointerDrag& drag,
        HWND releaseWindow,
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        std::optional<DockDropPreview>& dropPreview);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] static DockLayout BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics);
    [[nodiscard]] static bool IsMainWindow(HWND candidate, HWND mainWindow) noexcept;

    static void MoveSplitter(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics);
    [[nodiscard]] static bool ReorderDockedTab(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics);
#endif
};

} // namespace kb::editor
