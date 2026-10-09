#include "rendering/InspectorAddComponentOverlayWindow.hpp"

#if defined(_WIN32)
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/InspectorPanelRenderer.hpp"

#include <algorithm>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kOverlayClassName[] = L"KBEditorInspectorAddComponentOverlay";

[[nodiscard]] RECT ResolveScreenBounds(HWND owner, RECT ownerBounds) noexcept {
    RECT result = EditorOverlayPopupWindow::OwnerClientToScreen(owner, ownerBounds);

    MONITORINFO monitor{ sizeof(monitor) };
    if (GetMonitorInfoW(MonitorFromRect(&result, MONITOR_DEFAULTTONEAREST), &monitor) != 0) {
        if (result.right > monitor.rcWork.right) {
            OffsetRect(&result, monitor.rcWork.right - result.right, 0);
        }
        if (result.left < monitor.rcWork.left) {
            OffsetRect(&result, monitor.rcWork.left - result.left, 0);
        }
        if (result.bottom > monitor.rcWork.bottom) {
            OffsetRect(&result, 0, monitor.rcWork.bottom - result.bottom);
        }
        if (result.top < monitor.rcWork.top) {
            OffsetRect(&result, 0, monitor.rcWork.top - result.top);
        }
    }
    return result;
}

} // namespace

InspectorAddComponentOverlayWindow::InspectorAddComponentOverlayWindow() noexcept
    : popup_(kOverlayClassName, *this) {}

void InspectorAddComponentOverlayWindow::Show(
    HWND owner,
    const RECT& inspectorContent,
    const EditorTheme& theme,
    EditorSceneContext& sceneContext) {
    const std::optional<RECT> desired = InspectorPanelRenderer::AddComponentOverlayRect(inspectorContent, sceneContext);
    if (owner == nullptr || !desired.has_value() || !popup_.Ensure(owner)) {
        Hide();
        return;
    }

    theme_ = theme;
    sceneContext_ = &sceneContext;
    const InspectorPanelState& inspector = sceneContext.Inspector();
    const std::string_view search =
        inspector.EditedProperty() == InspectorPropertyId::AddComponentSearch
        ? std::string_view{ inspector.EditBuffer() }
        : std::string_view{};
    const bool visualStateChanged =
        renderedSearch_ != search ||
        renderedCategory_ != inspector.AddComponentBrowserCategory() ||
        renderedScroll_ != inspector.AddComponentScroll() ||
        renderedSlide_ != inspector.AddComponentSlide() ||
        renderedScrollbarDragging_ != inspector.IsAddComponentScrollbarDragging();
    const bool placed = popup_.ShowAt(ResolveScreenBounds(owner, *desired));
    if (placed || visualStateChanged) {
        renderedSearch_.assign(search);
        renderedCategory_ = inspector.AddComponentBrowserCategory();
        renderedScroll_ = inspector.AddComponentScroll();
        renderedSlide_ = inspector.AddComponentSlide();
        renderedScrollbarDragging_ = inspector.IsAddComponentScrollbarDragging();
        popup_.Invalidate();
    }
}

void InspectorAddComponentOverlayWindow::Hide() noexcept {
    if (popup_.Shown() && sceneContext_ != nullptr) {
        sceneContext_->Inspector().EndAddComponentScrollbarDrag();
    }
    popup_.Hide();
}

void InspectorAddComponentOverlayWindow::HideForOwner(HWND owner) noexcept {
    if (popup_.Owner() == owner) {
        Hide();
    }
}

void InspectorAddComponentOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    if (sceneContext_ == nullptr) {
        return;
    }
    InspectorPanelRenderer::PaintAddComponentOverlay(dc, client, theme_, *sceneContext_);
}

void InspectorAddComponentOverlayWindow::HandlePointerDown(int x, int y) {
    if (sceneContext_ == nullptr) {
        return;
    }
    const RECT bounds = popup_.ClientBounds();
    const InspectorPanelRenderer::Hit hit =
        InspectorPanelRenderer::HitTestAddComponentOverlay(bounds, *sceneContext_, x, y);
    if (hit.kind == InspectorHitKind::ScrollbarThumb) {
        sceneContext_->Inspector().BeginAddComponentScrollbarDrag(y - static_cast<int>(hit.rect.top));
        SetCapture(popup_.Window());
    } else if (hit.kind == InspectorHitKind::ScrollbarTrack) {
        const InspectorPanelRenderer::AddComponentScrollInfo info =
            InspectorPanelRenderer::AddComponentOverlayScrollGeometry(bounds, *sceneContext_);
        static_cast<void>(sceneContext_->Inspector().SetAddComponentScroll(
            sceneContext_->Inspector().AddComponentScroll() + (y < info.thumb.top ? -104 : 104),
            info.maxScroll));
    } else {
        static_cast<void>(InspectorPanelInteraction::HandlePointerDown(*sceneContext_, hit, x, y));
    }

    if (!sceneContext_->Inspector().IsAddComponentBrowserOpen()) {
        const HWND owner = popup_.Owner();
        Hide();
        if (owner != nullptr) {
            InvalidateRect(owner, nullptr, FALSE);
        }
    } else {
        popup_.Invalidate();
        InvalidateRect(popup_.Owner(), nullptr, FALSE);
    }
}

void InspectorAddComponentOverlayWindow::HandlePointerMove(int x, int y) {
    if (sceneContext_ == nullptr) {
        return;
    }
    const RECT bounds = popup_.ClientBounds();
    if (sceneContext_->Inspector().IsAddComponentScrollbarDragging()) {
        const InspectorPanelRenderer::AddComponentScrollInfo info =
            InspectorPanelRenderer::AddComponentOverlayScrollGeometry(bounds, *sceneContext_);
        if (!info.active) {
            HandlePointerUp();
            return;
        }
        const int thumbHeight = std::max(1, static_cast<int>(info.thumb.bottom - info.thumb.top));
        const int travel = std::max(1, static_cast<int>(info.track.bottom - info.track.top) - thumbHeight);
        const int newThumbTop = y - sceneContext_->Inspector().AddComponentScrollbarGrabOffset();
        static_cast<void>(sceneContext_->Inspector().SetAddComponentScroll(
            (newThumbTop - static_cast<int>(info.track.top)) * info.maxScroll / travel,
            info.maxScroll));
        popup_.Invalidate();
        return;
    }
    const InspectorPanelRenderer::Hit hit =
        InspectorPanelRenderer::HitTestAddComponentOverlay(bounds, *sceneContext_, x, y);
    if (InspectorPanelInteraction::UpdateHover(*sceneContext_, hit)) {
        popup_.Invalidate();
    }
    TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, popup_.Window(), 0U };
    static_cast<void>(TrackMouseEvent(&track));
}

void InspectorAddComponentOverlayWindow::HandlePointerUp() noexcept {
    if (sceneContext_ != nullptr) {
        sceneContext_->Inspector().EndAddComponentScrollbarDrag();
    }
    if (popup_.Window() != nullptr && GetCapture() == popup_.Window()) {
        ReleaseCapture();
    }
}

void InspectorAddComponentOverlayWindow::HandleMouseWheel(int screenX, int screenY, int delta) {
    if (sceneContext_ == nullptr) {
        return;
    }
    POINT point{ screenX, screenY };
    ScreenToClient(popup_.Window(), &point);
    const RECT bounds = popup_.ClientBounds();
    if (!InspectorPanelRenderer::AddComponentOverlayListContains(bounds, *sceneContext_, point.x, point.y)) {
        return;
    }
    const InspectorPanelRenderer::AddComponentScrollInfo info =
        InspectorPanelRenderer::AddComponentOverlayScrollGeometry(bounds, *sceneContext_);
    if (!info.active) {
        return;
    }
    const int direction = delta > 0 ? 1 : -1;
    static_cast<void>(sceneContext_->Inspector().SetAddComponentScroll(
        sceneContext_->Inspector().AddComponentScroll() - direction * 52,
        info.maxScroll));
    popup_.Invalidate();
}

bool InspectorAddComponentOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
        HandlePointerDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return true;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        HandlePointerUp();
        return true;
    case WM_MOUSEMOVE:
        HandlePointerMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return true;
    case WM_MOUSELEAVE:
        if (sceneContext_ != nullptr) {
            static_cast<void>(InspectorPanelInteraction::UpdateHover(*sceneContext_, {}));
            popup_.Invalidate();
        }
        return true;
    case WM_MOUSEWHEEL:
        HandleMouseWheel(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), GET_WHEEL_DELTA_WPARAM(wparam));
        return true;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
