#pragma once

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

class DockDropPreviewState {
public:
    DockDropPreviewState() = delete;

#if defined(_WIN32)
    // Where a released panel would land: a tab group of a floating window under the pointer, else the dock
    // workspace. The window of the dragged panel is not a target for it.
    static void Update(
        POINT screen,
        HWND mainWindow,
        std::uint32_t draggedPanelId,
        EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        std::optional<DockDropPreview>& dropPreview);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] static DockLayout BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics);
    [[nodiscard]] static std::optional<DockDropPreview> ResolveFloatingTarget(POINT screen, HWND mainWindow, std::uint32_t draggedPanelId, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics);
    [[nodiscard]] static bool SamePreview(const std::optional<DockDropPreview>& lhs, const std::optional<DockDropPreview>& rhs) noexcept;
    [[nodiscard]] static bool SameRect(const DockRect& lhs, const DockRect& rhs) noexcept;
#endif
};

} // namespace kb::editor
