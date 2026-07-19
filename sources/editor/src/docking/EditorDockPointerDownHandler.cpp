#include "docking/EditorDockPointerDownHandler.hpp"

#if defined(_WIN32)

#include "docking/DockMainLayoutResolver.hpp"
#include "docking/DockPointerDragFactory.hpp"
#include "windowing/FloatingWindowControlInteractor.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] const DockLeafLayout* FindLeaf(const DockLayout& layout, std::uint32_t leafId) noexcept {
    for (const DockLeafLayout& leaf : layout.leaves) {
        if (leaf.leafId == leafId) {
            return &leaf;
        }
    }
    return nullptr;
}

} // namespace

bool EditorDockPointerDownHandler::Handle(
    HWND window,
    int x,
    int y,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    std::optional<DockPointerDrag>& drag) {
    if (IsMainWindow(window, mainWindow)) {
        return HandleMainWindowDown(window, x, y, dockModel, metrics, drag);
    }

    HandleFloatingWindowDown(window, x, y, floatingWindows, metrics, drag);
    return false;
}

bool EditorDockPointerDownHandler::IsMainWindow(HWND window, HWND mainWindow) noexcept {
    return window == mainWindow;
}

bool EditorDockPointerDownHandler::HandleMainWindowDown(HWND window, int x, int y, EditorDockModel& dockModel, const EditorMetrics& metrics, std::optional<DockPointerDrag>& drag) {
    const DockLayout layout = DockMainLayoutResolver::Resolve(window, dockModel, metrics);
    const DockHit hit = dockModel.Queries().HitTest(layout, x, y);
    if (hit.kind == DockHitKind::None) {
        return false;
    }

    const DockLeafLayout* leaf = FindLeaf(layout, hit.leafId);
    const bool activePanelChanged = leaf != nullptr && leaf->activePanelId != hit.panelId;
    dockModel.Commands().ActivatePanel(hit.panelId);
    drag = DockPointerDragFactory::FromDockHit(window, layout, hit, x, y);
    if (activePanelChanged && leaf != nullptr) {
        InvalidateRect(window, nullptr, FALSE);
    }
    // A dock element (tab or splitter) was hit — the DockHitKind::None case
    // already returned false above. Report the click as consumed by the dock
    // system (not just when the active panel changed) so the caller treats tab
    // switches, re-clicks of the active tab and splitter drags as layout actions,
    // never as empty-space clicks that would clear the scene selection.
    return true;
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
