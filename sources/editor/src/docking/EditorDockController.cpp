#include "docking/EditorDockController.hpp"

#if defined(_WIN32)
#include "docking/DockDragOperationHandler.hpp"
#include "docking/DockPointerDragFactory.hpp"
#include "windowing/FloatingWindowControlInteractor.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <algorithm>

namespace kb::editor {

void EditorDockController::Configure(HWND mainWindow, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) noexcept {
    mainWindow_ = mainWindow;
    dockModel_ = &dockModel;
    floatingWindows_ = &floatingWindows;
    metrics_ = &metrics;
}

const DockDropPreview* EditorDockController::DropPreview() const noexcept {
    return dropPreview_ ? &*dropPreview_ : nullptr;
}

void EditorDockController::HandlePointerDown(HWND window, int x, int y) {
    if (!Ready()) {
        return;
    }

    SetFocus(window);
    SetCapture(window);

    if (IsMainWindow(window)) {
        const DockLayout layout = BuildMainLayout();
        const DockHit hit = dockModel_->HitTest(layout, x, y);
        if (hit.kind != DockHitKind::None) {
            dockModel_->ActivatePanel(hit.panelId);
            drag_ = DockPointerDragFactory::FromDockHit(window, layout, hit);
            InvalidateMain();
        }
        return;
    }

    const std::uint32_t panelId = floatingWindows_->PanelId(window);
    RECT client{};
    GetClientRect(window, &client);
    if (panelId != 0 && FloatingWindowControlInteractor{}.HandlePointerDown(window, *metrics_, x, y)) {
        return;
    }

    if (panelId != 0 && y <= metrics_->tabStripHeight + 2 && x < client.right - FloatingWindowControlLayout::TotalWidth(*metrics_)) {
        drag_ = DockPointerDragFactory::FromFloatingWindow(window, panelId, x, y, *metrics_);
    }
}

void EditorDockController::HandlePointerMove(HWND window, int x, int y) {
    if (!Ready()) {
        return;
    }

    if (!drag_.has_value()) {
        UpdateHoverCursor(window, x, y);
        return;
    }

    DockDragOperationHandler::Move(*drag_, window, x, y, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, dropPreview_);
}

void EditorDockController::HandlePointerUp(HWND window) {
    ReleaseCapture();

    if (!Ready() || !drag_.has_value()) {
        return;
    }

    const DockPointerDrag drag = *drag_;
    drag_.reset();
    DockDragOperationHandler::Complete(drag, window, mainWindow_, *dockModel_, *floatingWindows_, *metrics_, dropPreview_);
}

void EditorDockController::UpdateHoverCursor(HWND window, int x, int y) const {
    if (!Ready() || !IsMainWindow(window)) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    const DockLayout layout = BuildMainLayout();
    const DockHit hit = dockModel_->HitTest(layout, x, y);
    if (hit.kind == DockHitKind::Splitter) {
        const auto it = std::find_if(layout.splitters.begin(), layout.splitters.end(), [hit](const DockSplitterLayout& splitter) {
            return splitter.nodeId == hit.splitterNodeId;
        });
        SetCursor(LoadCursor(nullptr, it != layout.splitters.end() && it->axis == DockSplitAxis::Vertical ? IDC_SIZENS : IDC_SIZEWE));
    } else {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }
}

bool EditorDockController::Ready() const noexcept {
    return mainWindow_ != nullptr && dockModel_ != nullptr && floatingWindows_ != nullptr && metrics_ != nullptr;
}

bool EditorDockController::IsMainWindow(HWND window) const noexcept {
    return window == mainWindow_;
}

DockLayout EditorDockController::BuildMainLayout() const {
    RECT client{};
    GetClientRect(mainWindow_, &client);
    return dockModel_->BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics_->menuHeight,
        metrics_->toolbarHeight,
        metrics_->tabStripHeight,
        metrics_->tabMinWidth,
        metrics_->tabWidth,
        metrics_->splitterSize,
        metrics_->panelPadding);
}

void EditorDockController::InvalidateMain() const {
    InvalidateRect(mainWindow_, nullptr, FALSE);
}

} // namespace kb::editor

#endif
