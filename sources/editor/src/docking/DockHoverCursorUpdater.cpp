#include "docking/DockHoverCursorUpdater.hpp"

#if defined(_WIN32)

#include "docking/DockMainLayoutResolver.hpp"

#include <algorithm>

namespace kb::editor {

void DockHoverCursorUpdater::Update(HWND window, int x, int y, HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics) {
    if (!IsMainWindow(window, mainWindow)) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow, dockModel, metrics);
    const DockHit hit = dockModel.Queries().HitTest(layout, x, y);
    if (hit.kind != DockHitKind::Splitter) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    const auto it = std::find_if(layout.splitters.begin(), layout.splitters.end(), [hit](const DockSplitterLayout& splitter) {
        return splitter.nodeId == hit.splitterNodeId;
    });
    SetCursor(LoadCursor(nullptr, it != layout.splitters.end() && it->axis == DockSplitAxis::Vertical ? IDC_SIZENS : IDC_SIZEWE));
}

bool DockHoverCursorUpdater::IsMainWindow(HWND window, HWND mainWindow) noexcept {
    return window == mainWindow;
}

} // namespace kb::editor

#endif
