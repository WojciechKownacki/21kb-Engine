#include "docking/EditorDockController.hpp"

#if defined(_WIN32)
#include "docking/DockTabIndexResolver.hpp"
#include "windowing/FloatingWindowControlInteractor.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] bool SameRect(const DockRect& lhs, const DockRect& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}

[[nodiscard]] bool SamePreview(const std::optional<DockDropPreview>& lhs, const std::optional<DockDropPreview>& rhs) noexcept {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }

    return lhs->zone == rhs->zone && lhs->kind == rhs->kind && lhs->leafId == rhs->leafId && SameRect(lhs->rect, rhs->rect);
}

[[nodiscard]] DockDropPreview DefaultFloatingReturnTarget() noexcept {
    return DockDropPreview{ .zone = DockDropZone::Bottom };
}

} // namespace

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
            StartDockedDrag(window, layout, hit);
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
        StartFloatingDrag(window, panelId, x, y);
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

    PointerDrag& drag = *drag_;
    if (IsMainWindow(drag.sourceWindow)) {
        const DockLayout layout = BuildMainLayout();
        if (drag.kind == DockHitKind::Splitter) {
            dockModel_->ResizeSplitter(drag.splitterNodeId, x, y, layout);
            InvalidateMain();
            return;
        }
    }

    if (drag.kind != DockHitKind::Tab || drag.panelId == 0) {
        return;
    }

    POINT screen{ x, y };
    ClientToScreen(window, &screen);

    if (!drag.detached && drag.sourceStrip.Contains(x, y)) {
        const DockLayout layout = BuildMainLayout();
        const std::uint32_t newIndex = DockTabIndexResolver{}.Resolve(layout, drag.sourceLeafId, x);
        if (newIndex != drag.sourceTabIndex) {
            dockModel_->ReorderPanelInLeaf(drag.panelId, drag.sourceLeafId, newIndex);
            drag.sourceTabIndex = newIndex;
            InvalidateMain();
        }
        return;
    }

    if (!drag.detached) {
        drag.offsetX = EditorFloatingWindowManager::DragWidth / 2;
        drag.offsetY = EditorFloatingWindowManager::StripCursorY;
        const DockPanel* panel = dockModel_->FindPanel(drag.panelId);
        if (panel != nullptr && panel->detachable) {
            DockRect floatingRect = panel->floatingRect;
            floatingRect.x = screen.x - drag.offsetX;
            floatingRect.y = screen.y - drag.offsetY;
            floatingRect.width = EditorFloatingWindowManager::DragWidth;
            floatingRect.height = EditorFloatingWindowManager::DragHeight;
            const std::string title = panel->title;

            dockModel_->UndockPanel(drag.panelId, floatingRect);
            drag.detached = floatingWindows_->Create(drag.panelId, title, floatingRect);
            if (!drag.detached) {
                dockModel_->DockPanelTo(drag.panelId, DefaultFloatingReturnTarget());
            }
        }
    }

    if (!drag.detached) {
        return;
    }

    if (HWND floating = floatingWindows_->WindowForPanel(drag.panelId); floating != nullptr) {
        RECT rect{};
        GetWindowRect(floating, &rect);
        SetWindowPos(floating, HWND_TOP, screen.x - drag.offsetX, screen.y - drag.offsetY, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE);
        dockModel_->MoveFloatingPanel(drag.panelId, screen.x - drag.offsetX, screen.y - drag.offsetY);
    }

    UpdateDropPreviewAtScreenPoint(screen);
}

void EditorDockController::HandlePointerUp(HWND window) {
    ReleaseCapture();

    if (!Ready() || !drag_.has_value()) {
        return;
    }

    const PointerDrag drag = *drag_;
    drag_.reset();

    if (drag.detached && drag.panelId != 0) {
        POINT screen{};
        GetCursorPos(&screen);
        POINT mainPoint{ screen.x, screen.y };
        ScreenToClient(mainWindow_, &mainPoint);
        const DockLayout layout = BuildMainLayout();
        const std::optional<DockDropPreview> preview = dockModel_->ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
        if (preview.has_value()) {
            floatingWindows_->Destroy(drag.panelId);
            dockModel_->DockPanelTo(drag.panelId, *preview);
        } else {
            if (const std::optional<DockRect> rect = floatingWindows_->RectForPanel(drag.panelId); rect.has_value()) {
                dockModel_->MoveFloatingPanel(drag.panelId, rect->x, rect->y);
                dockModel_->ResizeFloatingPanel(drag.panelId, rect->width, rect->height);
            }
        }
    }

    dropPreview_.reset();
    InvalidateMain();
    if (!IsMainWindow(window)) {
        InvalidateRect(window, nullptr, FALSE);
    }
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

void EditorDockController::StartDockedDrag(HWND window, const DockLayout& layout, const DockHit& hit) {
    DockRect sourceStrip{};
    std::uint32_t sourceTabIndex = 0;
    if (hit.kind == DockHitKind::Tab) {
        for (const DockLeafLayout& leaf : layout.leaves) {
            if (leaf.leafId == hit.leafId) {
                sourceStrip = leaf.tabStrip;
                break;
            }
        }

        std::uint32_t leafIndex = 0;
        for (const DockPanelLayout& panel : layout.panels) {
            if (panel.leafId != hit.leafId) {
                continue;
            }
            if (panel.panelId == hit.panelId) {
                sourceTabIndex = leafIndex;
                break;
            }
            ++leafIndex;
        }
    }

    drag_ = PointerDrag{
        .kind = hit.kind,
        .panelId = hit.panelId,
        .offsetX = 18,
        .offsetY = 14,
        .splitterNodeId = hit.splitterNodeId,
        .sourceLeafId = hit.leafId,
        .sourceTabIndex = sourceTabIndex,
        .sourceStrip = sourceStrip,
        .sourceWindow = window,
    };
}

void EditorDockController::StartFloatingDrag(HWND window, std::uint32_t panelId, int x, int y) {
    POINT screen{ x, y };
    ClientToScreen(window, &screen);
    RECT frame{};
    GetWindowRect(window, &frame);
    drag_ = PointerDrag{
        .kind = DockHitKind::Tab,
        .panelId = panelId,
        .offsetX = screen.x - frame.left,
        .offsetY = screen.y - frame.top,
        .sourceLeafId = 0,
        .sourceTabIndex = 0,
        .sourceStrip = FloatingWindowControlLayout::StripDragRect(*metrics_, frame.right - frame.left),
        .sourceWindow = window,
        .detached = true,
    };
}

void EditorDockController::UpdateDropPreviewAtScreenPoint(POINT screen) {
    POINT mainPoint{ screen.x, screen.y };
    ScreenToClient(mainWindow_, &mainPoint);
    const DockLayout layout = BuildMainLayout();
    const std::optional<DockDropPreview> nextPreview = dockModel_->ResolveDropPreview(layout, mainPoint.x, mainPoint.y);
    if (!SamePreview(dropPreview_, nextPreview)) {
        dropPreview_ = nextPreview;
        InvalidateMain();
    }
}

} // namespace kb::editor

#endif
