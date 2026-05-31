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

#if defined(_WIN32)

class EditorAssetBrowserPointerPanelResolver {
public:
    EditorAssetBrowserPointerPanelResolver() = delete;

    [[nodiscard]] static std::optional<RECT> ResolveContent(
        HWND sourceWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics);

    [[nodiscard]] static RECT ResolveDeleteConfirmOverlayBounds(
        HWND sourceWindow,
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorMetrics& metrics,
        const RECT& fallback) noexcept;
};

#endif

} // namespace kb::editor
