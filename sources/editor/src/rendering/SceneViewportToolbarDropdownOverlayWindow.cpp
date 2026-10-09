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

SceneViewportToolbarDropdownOverlayWindow::SceneViewportToolbarDropdownOverlayWindow() noexcept
    : popup_(kSceneViewportToolbarDropdownOverlayClassName, *this) {}

void SceneViewportToolbarDropdownOverlayWindow::Show(HWND parent, const RECT& sceneContent, std::uint64_t panelId, const EditorTheme& theme, const EditorSceneContext& sceneContext) {
    if (parent == nullptr || !popup_.Ensure(parent)) {
        Hide();
        return;
    }

    const EditorViewportToolbarDropdown dropdown = sceneContext.ViewportPreview(panelId).ToolbarDropdown();
    if (dropdown == EditorViewportToolbarDropdown::None && !TerrainPopupOpen()) {
        Hide();
        return;
    }

    sceneContent_ = sceneContent;
    panelId_ = panelId;
    theme_ = theme;
    sceneContext_ = &sceneContext;

    const RECT nextBounds = ResolveScreenBounds();
    if (EmptyRect(nextBounds)) {
        Hide();
        return;
    }

    if (popup_.ShowAt(nextBounds)) {
        popup_.Invalidate();
    }
}

void SceneViewportToolbarDropdownOverlayWindow::Hide() noexcept {
    popup_.Hide();
    hoveredItem_ = -1;
}

RECT SceneViewportToolbarDropdownOverlayWindow::ResolveScreenBounds() const noexcept {
    const HWND owner = popup_.Owner();
    if (owner == nullptr || sceneContext_ == nullptr || IsWindow(owner) == 0) {
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

    return EditorOverlayPopupWindow::OwnerClientToScreen(owner, popup);
}

void SceneViewportToolbarDropdownOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    if (sceneContext_ == nullptr) {
        return;
    }

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
    if (sceneContext_ == nullptr || popup_.Window() == nullptr) {
        return -1;
    }
    const RECT client = popup_.ClientBounds();
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

bool SceneViewportToolbarDropdownOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        popup_.ForwardToOwner(message, wparam, lparam);
        if (sceneContext_ == nullptr ||
            (sceneContext_->ViewportPreview(panelId_).ToolbarDropdown() == EditorViewportToolbarDropdown::None &&
             !TerrainPopupOpen())) {
            Hide();
        } else {
            popup_.Invalidate();
        }
        return true;
    case WM_MOUSEMOVE: {
        const int index = ItemIndexAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (hoveredItem_ != index) {
            hoveredItem_ = index;
            popup_.Invalidate();
        }
        TRACKMOUSEEVENT track{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, popup_.Window(), 0U };
        static_cast<void>(TrackMouseEvent(&track));
        return true;
    }
    case WM_MOUSELEAVE:
        if (hoveredItem_ != -1) {
            hoveredItem_ = -1;
            popup_.Invalidate();
        }
        return true;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
