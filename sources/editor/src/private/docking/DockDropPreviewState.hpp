#pragma once

#include "docking/EditorDockModel.hpp"
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
    static void Update(
        POINT screen,
        HWND mainWindow,
        EditorDockModel& dockModel,
        const EditorMetrics& metrics,
        std::optional<DockDropPreview>& dropPreview);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] static DockLayout BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics);
    [[nodiscard]] static bool SamePreview(const std::optional<DockDropPreview>& lhs, const std::optional<DockDropPreview>& rhs) noexcept;
    [[nodiscard]] static bool SameRect(const DockRect& lhs, const DockRect& rhs) noexcept;
#endif
};

} // namespace kb::editor
