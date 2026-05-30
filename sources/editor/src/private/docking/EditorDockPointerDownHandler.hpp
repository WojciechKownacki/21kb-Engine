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

class EditorDockPointerDownHandler {
public:
    EditorDockPointerDownHandler() = delete;

#if defined(_WIN32)
    static void Handle(
        HWND window,
        int x,
        int y,
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        std::optional<DockPointerDrag>& drag);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] static bool IsMainWindow(HWND window, HWND mainWindow) noexcept;
    static void HandleMainWindowDown(HWND window, int x, int y, EditorDockModel& dockModel, const EditorMetrics& metrics, std::optional<DockPointerDrag>& drag);
    static void HandleFloatingWindowDown(HWND window, int x, int y, EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, std::optional<DockPointerDrag>& drag);
#endif
};

} // namespace kb::editor
