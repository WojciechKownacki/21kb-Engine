#include "docking/EditorDockPointerDownHandler.hpp"

#if defined(_WIN32)

#include "docking/DockMainLayoutResolver.hpp"
#include "docking/DockPointerDragFactory.hpp"
#include "windowing/FloatingWindowControlInteractor.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

namespace kb::editor {

void EditorDockPointerDownHandler::Handle(
    HWND window,
    int x,
    int y,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::optional<DockPointerDrag>& drag) {
    if (IsMainWindow(window, mainWindow)) {
        HandleMainWindowDown(window, x, y, dockModel, metrics, drag);
        return;
    }

    HandleFloatingWindowDown(window, x, y, floatingWindows, metrics, drag);
}

bool EditorDockPointerDownHandler::IsMainWindow(HWND window, HWND mainWindow) noexcept {
    return window == mainWindow;
}

void EditorDockPointerDownHandler::HandleMainWindowDown(HWND window, int x, int y, EditorDockModel& dockModel, const EditorMetrics& metrics, std::optional<DockPointerDrag>& drag) {
    const DockLayout layout = DockMainLayoutResolver::Resolve(window, dockModel, metrics);
    const DockHit hit = dockModel.Queries().HitTest(layout, x, y);
    if (hit.kind == DockHitKind::None) {
        return;
    }

    dockModel.Commands().ActivatePanel(hit.panelId);
    drag = DockPointerDragFactory::FromDockHit(window, layout, hit, x, y);
    InvalidateRect(window, nullptr, FALSE);
}

void EditorDockPointerDownHandler::HandleFloatingWindowDown(HWND window, int x, int y, EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, std::optional<DockPointerDrag>& drag) {
    const std::uint32_t panelId = floatingWindows.Queries().PanelId(window);
    RECT client{};
    GetClientRect(window, &client);
    if (panelId != 0 && FloatingWindowControlInteractor{}.HandlePointerDown(window, metrics, x, y)) {
        return;
    }

    if (panelId != 0 && y <= metrics.tabStripHeight + 2 && x < client.right - FloatingWindowControlLayout::TotalWidth(metrics)) {
        drag = DockPointerDragFactory::FromFloatingWindow(window, panelId, x, y, metrics);
    }
}

} // namespace kb::editor

#endif
