#include "docking/DockFloatingDragOperation.hpp"

#if defined(_WIN32)

#include <algorithm>
#include <string>

namespace kb::editor {

void DockFloatingDragOperation::EnsureDetached(
    DockPointerDrag& drag,
    POINT screen,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    if (drag.detached) {
        return;
    }

    drag.offsetX = metrics.tabWidth / 2;
    drag.offsetY = metrics.tabStripHeight / 2;

    const DockPanel* panel = dockModel.Queries().FindPanel(drag.panelId);
    if (panel == nullptr || !panel->detachable) {
        return;
    }

    DockRect floatingRect = panel->floatingRect;
    floatingRect.x = screen.x - drag.offsetX;
    floatingRect.y = static_cast<int>(std::max<LONG>(0, screen.y - drag.offsetY));
    floatingRect.width = EditorFloatingWindowManager::DragWidth;
    floatingRect.height = EditorFloatingWindowManager::DragHeight;
    const std::string title = panel->title;

    dockModel.Commands().UndockPanel(drag.panelId, floatingRect);
    drag.detached = floatingWindows.Commands().Create(drag.panelId, title, floatingRect);
    if (!drag.detached) {
        dockModel.Commands().DockPanelTo(drag.panelId, DefaultFloatingReturnTarget());
    }
}

void DockFloatingDragOperation::MoveWindow(DockPointerDrag& drag, POINT screen, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    HWND floating = floatingWindows.Queries().WindowForPanel(drag.panelId);
    if (floating == nullptr) {
        return;
    }

    RECT rect{};
    GetWindowRect(floating, &rect);
    const int x = screen.x - drag.offsetX;
    const int y = screen.y - drag.offsetY;
    if (x != rect.left || y != rect.top) {
        SetWindowPos(floating, HWND_TOP, x, y, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE);
    }
    dockModel.Commands().MoveFloatingPanel(drag.panelId, x, y);
}

DockDropPreview DockFloatingDragOperation::DefaultFloatingReturnTarget() noexcept {
    return DockDropPreview{ .zone = DockDropZone::Bottom };
}

} // namespace kb::editor

#endif
