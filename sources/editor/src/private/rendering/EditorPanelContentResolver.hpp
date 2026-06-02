#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct EditorResolvedPanelContent {
    RECT content{};
    std::uint32_t panelId = 0;
};

class EditorPanelContentResolver {
public:
    EditorPanelContentResolver() = delete;

    [[nodiscard]] static std::optional<RECT> Resolve(
        DockPanelKind kind,
        HWND sourceWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics);

    [[nodiscard]] static std::optional<EditorResolvedPanelContent> ResolvePanel(
        DockPanelKind kind,
        HWND sourceWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics);
};

#endif

} // namespace kb::editor
