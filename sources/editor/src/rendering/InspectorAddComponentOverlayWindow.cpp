#include "rendering/InspectorAddComponentOverlayWindow.hpp"

#if defined(_WIN32)
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/GdiBackBufferRenderer.hpp"
#include "rendering/InspectorPanelRenderer.hpp"

#include <algorithm>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kOverlayClassName[] = L"KBEditorInspectorAddComponentOverlay";

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] RECT ClientBounds(HWND window) noexcept {
    RECT client{};
    if (window != nullptr) {
        GetClientRect(window, &client);
    }
    return client;
}

[[nodiscard]] RECT ResolveScreenBounds(HWND owner, RECT ownerBounds) noexcept {
    POINT origin{ ownerBounds.left, ownerBounds.top };
    ClientToScreen(owner, &origin);
    const int width = static_cast<int>(ownerBounds.right - ownerBounds.left);
    const int height = static_cast<int>(ownerBounds.bottom - ownerBounds.top);
    RECT result{ origin.x, origin.y, origin.x + width, origin.y + height };

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

InspectorAddComponentOverlayWindow::~InspectorAddComponentOverlayWindow() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

void InspectorAddComponentOverlayWindow::Show(
    HWND owner,
    const RECT& inspectorContent,
    const EditorTheme& theme,
    EditorSceneContext& sceneContext) {
    const std::optional<RECT> desired = InspectorPanelRenderer::AddComponentOverlayRect(inspectorContent, sceneContext);
    if (owner == nullptr || !desired.has_value() || !EnsureWindow(owner)) {
        Hide();
        return;
    }

    owner_ = owner;
    theme_ = theme;
    sceneContext_ = &sceneContext;
    const RECT nextBounds = ResolveScreenBounds(owner, *desired);
    const bool movedOrResized = !SameRect(screenBounds_, nextBounds);
    const bool firstShow = !shown_;
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
    if (movedOrResized || firstShow) {
        screenBounds_ = nextBounds;
        SetWindowPos(
            window_,
            HWND_TOP,
            screenBounds_.left,
            screenBounds_.top,
            screenBounds_.right - screenBounds_.left,
            screenBounds_.bottom - screenBounds_.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        shown_ = true;
    }
    if (movedOrResized || firstShow || visualStateChanged) {
        renderedSearch_.assign(search);
        renderedCategory_ = inspector.AddComponentBrowserCategory();
        renderedScroll_ = inspector.AddComponentScroll();
        renderedSlide_ = inspector.AddComponentSlide();
        renderedScrollbarDragging_ = inspector.IsAddComponentScrollbarDragging();
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void InspectorAddComponentOverlayWindow::Hide() noexcept {
    if (shown_ && sceneContext_ != nullptr) {
        sceneContext_->Inspector().EndAddComponentScrollbarDrag();
    }
    if (window_ != nullptr && GetCapture() == window_) {
        ReleaseCapture();
    }
    if (window_ != nullptr && IsWindow(window_) != 0) {
        ShowWindow(window_, SW_HIDE);
    }
    shown_ = false;
}

void InspectorAddComponentOverlayWindow::HideForOwner(HWND owner) noexcept {
    if (owner_ == owner) {
        Hide();
    }
}

bool InspectorAddComponentOverlayWindow::EnsureWindow(HWND owner) {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        if (owner_ != owner) {
            SetWindowLongPtrW(window_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
        }
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &InspectorAddComponentOverlayWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kOverlayClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kOverlayClassName,
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        owner,
        nullptr,
        windowClass.hInstance,
        this);
    return window_ != nullptr;
}

void InspectorAddComponentOverlayWindow::Paint(HDC dc) const {
    if (sceneContext_ == nullptr) {
        return;
    }
    InspectorPanelRenderer::PaintAddComponentOverlay(dc, ClientBounds(window_), theme_, *sceneContext_);
}

void InspectorAddComponentOverlayWindow::HandlePointerDown(int x, int y) {
    if (sceneContext_ == nullptr) {
        return;
    }
    const RECT bounds = ClientBounds(window_);
    const InspectorPanelRenderer::Hit hit =
        InspectorPanelRenderer::HitTestAddComponentOverlay(bounds, *sceneContext_, x, y);
    if (hit.kind == InspectorHitKind::ScrollbarThumb) {
        sceneContext_->Inspector().BeginAddComponentScrollbarDrag(y - static_cast<int>(hit.rect.top));
        SetCapture(window_);
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
        const HWND owner = owner_;
        Hide();
        if (owner != nullptr) {
            InvalidateRect(owner, nullptr, FALSE);
        }
    } else {
        InvalidateRect(window_, nullptr, FALSE);
        InvalidateRect(owner_, nullptr, FALSE);
    }
}

void InspectorAddComponentOverlayWindow::HandlePointerMove(int x, int y) {
    if (sceneContext_ == nullptr) {
        return;
    }
    const RECT bounds = ClientBounds(window_);
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
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    const InspectorPanelRenderer::Hit hit =
        InspectorPanelRenderer::HitTestAddComponentOverlay(bounds, *sceneContext_, x, y);
    if (InspectorPanelInteraction::UpdateHover(*sceneContext_, hit)) {
        InvalidateRect(window_, nullptr, FALSE);
    }
    TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0U };
    static_cast<void>(TrackMouseEvent(&track));
}

void InspectorAddComponentOverlayWindow::HandlePointerUp() noexcept {
    if (sceneContext_ != nullptr) {
        sceneContext_->Inspector().EndAddComponentScrollbarDrag();
    }
    if (window_ != nullptr && GetCapture() == window_) {
        ReleaseCapture();
    }
}

void InspectorAddComponentOverlayWindow::HandleMouseWheel(int screenX, int screenY, int delta) {
    if (sceneContext_ == nullptr) {
        return;
    }
    POINT point{ screenX, screenY };
    ScreenToClient(window_, &point);
    const RECT bounds = ClientBounds(window_);
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
    InvalidateRect(window_, nullptr, FALSE);
}

LRESULT CALLBACK InspectorAddComponentOverlayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* overlay = reinterpret_cast<InspectorAddComponentOverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        GdiBackBufferRenderer::Paint(
            window,
            [](const GdiBackBufferPaintContext& paint, void* context) {
                auto* target = static_cast<InspectorAddComponentOverlayWindow*>(context);
                if (target != nullptr) {
                    target->Paint(paint.dc);
                }
            },
            overlay);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (overlay != nullptr) {
            overlay->HandlePointerDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (overlay != nullptr) {
            overlay->HandlePointerUp();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (overlay != nullptr) {
            overlay->HandlePointerMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        if (overlay != nullptr && overlay->sceneContext_ != nullptr) {
            static_cast<void>(InspectorPanelInteraction::UpdateHover(*overlay->sceneContext_, {}));
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (overlay != nullptr) {
            overlay->HandleMouseWheel(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), GET_WHEEL_DELTA_WPARAM(wparam));
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (overlay != nullptr) {
            overlay->HandlePointerUp();
        }
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return TRUE;
    case WM_NCDESTROY:
        if (overlay != nullptr && overlay->window_ == window) {
            overlay->window_ = nullptr;
            overlay->owner_ = nullptr;
            overlay->sceneContext_ = nullptr;
            overlay->shown_ = false;
            overlay->screenBounds_ = RECT{};
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
