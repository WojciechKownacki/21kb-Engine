#include "rendering/SceneViewportToolbarDropdownOverlayWindow.hpp"

#if defined(_WIN32)
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <cmath>
#include <windowsx.h>

namespace kb::editor {
namespace {

constexpr wchar_t kSceneViewportToolbarDropdownOverlayClassName[] = L"KBEditorSceneViewportToolbarDropdownOverlay";
constexpr int kDropdownPadding = 5;
constexpr int kDropdownItemHeight = 24;

[[nodiscard]] bool TerrainPopupOpen() noexcept {
    const EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    return tool.brushMenuOpen || tool.brushShapeMenuOpen;
}

[[nodiscard]] bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

[[nodiscard]] bool EmptyRect(const RECT& rect) noexcept {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

[[nodiscard]] bool NearlyEqual(float a, float b) noexcept {
    return std::abs(a - b) <= 0.001F;
}

[[nodiscard]] RECT ItemRect(const RECT& client, std::size_t index, std::size_t count) noexcept {
    const int availableWidth = static_cast<int>(client.right - client.left) - (kDropdownPadding * 2);
    const int width = std::max(1, availableWidth / static_cast<int>(std::max<std::size_t>(1U, count)));
    const int left = client.left + kDropdownPadding + (static_cast<int>(index) * width);
    return RECT{
        left,
        client.top + kDropdownPadding,
        left + width,
        client.top + kDropdownPadding + kDropdownItemHeight,
    };
}

} // namespace

SceneViewportToolbarDropdownOverlayWindow::~SceneViewportToolbarDropdownOverlayWindow() {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        const HWND window = window_;
        window_ = nullptr;
        DestroyWindow(window);
    }
}

void SceneViewportToolbarDropdownOverlayWindow::Show(HWND parent, const RECT& sceneContent, std::uint64_t panelId, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (parent == nullptr || !EnsureWindow(parent)) {
        Hide();
        return;
    }

    const EditorViewportToolbarDropdown dropdown = sceneContext.ViewportPreview(panelId).ToolbarDropdown();
    if (dropdown == EditorViewportToolbarDropdown::None && !TerrainPopupOpen()) {
        Hide();
        return;
    }

    parent_ = parent;
    sceneContent_ = sceneContent;
    panelId_ = panelId;
    theme_ = theme;
    sceneContext_ = &sceneContext;

    const RECT nextBounds = ResolveScreenBounds();
    if (EmptyRect(nextBounds)) {
        Hide();
        return;
    }

    const bool movedOrResized = !SameRect(screenBounds_, nextBounds);
    const bool firstShow = !shown_;
    if (movedOrResized || firstShow) {
        static_cast<void>(MoveToCurrentBounds(true));
    }
    if (movedOrResized || firstShow) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void SceneViewportToolbarDropdownOverlayWindow::Hide() noexcept {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        ShowWindow(window_, SW_HIDE);
    }
    shown_ = false;
    hoveredItem_ = -1;
}

bool SceneViewportToolbarDropdownOverlayWindow::EnsureWindow(HWND parent) {
    if (window_ != nullptr && IsWindow(window_) != 0) {
        if (GetWindow(window_, GW_OWNER) != parent) {
            static_cast<void>(SetWindowLongPtrW(
                window_,
                GWLP_HWNDPARENT,
                reinterpret_cast<LONG_PTR>(parent)));
        }
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &SceneViewportToolbarDropdownOverlayWindow::WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = kSceneViewportToolbarDropdownOverlayClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kSceneViewportToolbarDropdownOverlayClassName,
        L"",
        WS_POPUP,
        0,
        0,
        1,
        1,
        parent,
        nullptr,
        windowClass.hInstance,
        this);
    return window_ != nullptr;
}

RECT SceneViewportToolbarDropdownOverlayWindow::ResolveScreenBounds() const noexcept {
    if (parent_ == nullptr || sceneContext_ == nullptr || IsWindow(parent_) == 0) {
        return {};
    }

    RECT popup{};
    if (TerrainPopupOpen()) {
        const TerrainViewportToolbarRects terrainRects = SceneViewportToolbarRenderer::ResolveTerrainTools(sceneContent_);
        popup = EditorTerrainService::ToolState().brushShapeMenuOpen
            ? terrainRects.brushShapeMenu
            : terrainRects.brushMenu;
    } else {
        popup = SceneViewportToolbarRenderer::Resolve(
            sceneContent_, sceneContext_->ViewportPreview(panelId_)).dropdownPanel;
    }
    if (EmptyRect(popup)) {
        return {};
    }

    POINT screen{ popup.left, popup.top };
    ClientToScreen(parent_, &screen);
    return RECT{
        screen.x,
        screen.y,
        screen.x + popup.right - popup.left,
        screen.y + popup.bottom - popup.top,
    };
}

bool SceneViewportToolbarDropdownOverlayWindow::MoveToCurrentBounds(bool showWindow) noexcept {
    if (window_ == nullptr || IsWindow(window_) == 0) {
        return false;
    }

    const RECT nextBounds = ResolveScreenBounds();
    if (EmptyRect(nextBounds)) {
        return false;
    }
    if (SameRect(screenBounds_, nextBounds) && shown_) {
        return false;
    }

    screenBounds_ = nextBounds;
    SetWindowPos(
        window_,
        HWND_TOP,
        screenBounds_.left,
        screenBounds_.top,
        screenBounds_.right - screenBounds_.left,
        screenBounds_.bottom - screenBounds_.top,
        SWP_NOACTIVATE | (showWindow ? SWP_SHOWWINDOW : 0U));
    shown_ = shown_ || showWindow;
    return true;
}

void SceneViewportToolbarDropdownOverlayWindow::Paint(HDC dc) const {
    if (sceneContext_ == nullptr) {
        return;
    }

    RECT client{};
    GetClientRect(window_, &client);
    if (TerrainPopupOpen()) {
        SceneViewportToolbarRenderer::PaintTerrainPopup(dc, client, theme_, *sceneContext_, hoveredItem_);
        return;
    }
    EditorDialogStyle::PaintSurface(dc, client, theme_);

    const EditorViewportPreviewState& state = sceneContext_->ViewportPreview(panelId_);
    const EditorViewportToolbarDropdown dropdown = state.ToolbarDropdown();
    const bool gridDropdown = dropdown == EditorViewportToolbarDropdown::GridSpacing;
    const bool rotationDropdown = dropdown == EditorViewportToolbarDropdown::RotationSnap;
    const std::size_t count = gridDropdown
        ? EditorViewportGridSpacingOptionCount()
        : (rotationDropdown ? EditorViewportRotationSnapOptionCount() : EditorViewportSnapStepOptionCount());
    const float active = gridDropdown ? state.GridSpacing() : (rotationDropdown ? state.RotationSnapDegrees() : state.SnapStep());
    for (std::size_t index = 0; index < count; ++index) {
        const float value = gridDropdown
            ? EditorViewportGridSpacingOption(index)
            : (rotationDropdown ? EditorViewportRotationSnapOption(index) : EditorViewportSnapStepOption(index));
        const bool selected = NearlyEqual(value, active);
        const bool hovered = static_cast<int>(index) == hoveredItem_;
        RECT item = ItemRect(client, index, count);
        item.top += 1;
        item.bottom -= 1;
        EditorDialogStyle::PaintButton(
            dc,
            item,
            theme_,
            gridDropdown
                ? EditorViewportGridSpacingLabel(value)
                : (rotationDropdown ? EditorViewportRotationSnapLabel(value) : EditorViewportSnapStepLabel(value)),
            selected ? EditorDialogButtonTone::Primary : EditorDialogButtonTone::Neutral,
            hovered);
    }
}

int SceneViewportToolbarDropdownOverlayWindow::ItemIndexAt(int clientX, int clientY) const noexcept {
    if (sceneContext_ == nullptr || window_ == nullptr || IsWindow(window_) == 0) {
        return -1;
    }
    RECT client{};
    GetClientRect(window_, &client);
    if (TerrainPopupOpen()) {
        const bool shapes = EditorTerrainService::ToolState().brushShapeMenuOpen;
        const int headerHeight = shapes ? 38 : 34;
        const int itemHeight = shapes ? 72 : 55;
        const int columnWidth = shapes ? 196 : 172;
        const int itemWidth = shapes ? 192 : 168;
        const int itemVisualHeight = shapes ? 68 : 51;
        const std::size_t count = shapes ? 6U : 8U;
        for (std::size_t index = 0U; index < count; ++index) {
            const int column = static_cast<int>(index % 2U);
            const int row = static_cast<int>(index / 2U);
            const RECT item{
                client.left + 6 + column * columnWidth,
                client.top + headerHeight + row * itemHeight,
                client.left + 6 + column * columnWidth + itemWidth,
                client.top + headerHeight + row * itemHeight + itemVisualHeight,
            };
            if (clientX >= item.left && clientX < item.right && clientY >= item.top && clientY < item.bottom) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }
    const EditorViewportPreviewState& state = sceneContext_->ViewportPreview(panelId_);
    const EditorViewportToolbarDropdown dropdown = state.ToolbarDropdown();
    const std::size_t count = dropdown == EditorViewportToolbarDropdown::GridSpacing
        ? EditorViewportGridSpacingOptionCount()
        : (dropdown == EditorViewportToolbarDropdown::RotationSnap ? EditorViewportRotationSnapOptionCount() : EditorViewportSnapStepOptionCount());
    for (std::size_t index = 0; index < count; ++index) {
        const RECT item = ItemRect(client, index, count);
        if (clientX >= item.left && clientX < item.right && clientY >= item.top && clientY < item.bottom) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void SceneViewportToolbarDropdownOverlayWindow::ForwardMouseMessage(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (parent_ == nullptr || IsWindow(parent_) == 0) {
        return;
    }
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ClientToScreen(window_, &point);
    ScreenToClient(parent_, &point);
    SendMessageW(parent_, message, wparam, MAKELPARAM(point.x, point.y));
}

LRESULT CALLBACK SceneViewportToolbarDropdownOverlayWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* overlay = reinterpret_cast<SceneViewportToolbarDropdownOverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (overlay != nullptr) {
            overlay->Paint(dc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (overlay != nullptr) {
            overlay->ForwardMouseMessage(message, wparam, lparam);
            if (overlay->sceneContext_ == nullptr ||
                (overlay->sceneContext_->ViewportPreview(overlay->panelId_).ToolbarDropdown() == EditorViewportToolbarDropdown::None &&
                 !TerrainPopupOpen())) {
                ShowWindow(window, SW_HIDE);
                overlay->shown_ = false;
            } else {
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (overlay != nullptr) {
            const int index = overlay->ItemIndexAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (overlay->hoveredItem_ != index) {
                overlay->hoveredItem_ = index;
                InvalidateRect(window, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0U };
            static_cast<void>(TrackMouseEvent(&track));
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        if (overlay != nullptr && overlay->hoveredItem_ != -1) {
            overlay->hoveredItem_ = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return TRUE;
    case WM_NCDESTROY:
        if (overlay != nullptr && overlay->window_ == window) {
            overlay->window_ = nullptr;
            overlay->parent_ = nullptr;
            overlay->sceneContext_ = nullptr;
            overlay->shown_ = false;
            overlay->hoveredItem_ = -1;
            overlay->screenBounds_ = RECT{};
            overlay->sceneContent_ = RECT{};
            overlay->panelId_ = 0U;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
