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

class DockWindowLayoutResolver {
public:
    DockWindowLayoutResolver() = delete;

#if defined(_WIN32)
    // The tabs of one window, in its client coordinates: the dock workspace for the main
    // window, the single tab group of a floating window, nothing for any other window.
    [[nodiscard]] static DockLayout Resolve(HWND window, HWND mainWindow, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics);
    // The tab group of a floating window with the given client area.
    [[nodiscard]] static DockLayout ResolveFloating(const RECT& client, std::uint32_t groupId, const EditorDockModel& dockModel, const EditorMetrics& metrics);
#endif
};

} // namespace kb::editor
