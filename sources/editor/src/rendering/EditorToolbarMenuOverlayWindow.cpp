#include "rendering/EditorToolbarMenuOverlayWindow.hpp"

#if defined(_WIN32)
#include "rendering/EditorToolbarRenderer.hpp"
#include "rendering/GdiDrawing.hpp"

#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] bool EmptyRect(const RECT& rect) noexcept {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

[[nodiscard]] EditorMenuRects ResolveOpenMenu(const RECT& menuBar, const EditorShellInteractionState& interaction) noexcept {
    return EditorToolbarRenderer::ResolveMenu(
        menuBar, interaction.OpenMenu(), interaction.MenuRowCount(interaction.OpenMenu()));
}

} // namespace

EditorToolbarMenuOverlayWindow::EditorToolbarMenuOverlayWindow() noexcept
    : popup_(ClassName, *this) {}

void EditorToolbarMenuOverlayWindow::Show(HWND owner, const RECT& menuBar, const EditorTheme& theme, const EditorShellInteractionState& interaction) {
    if (owner == nullptr || interaction.OpenMenu() == EditorMenuCommand::None || !popup_.Ensure(owner)) {
        Hide();
        return;
    }

    const RECT paintBounds = EditorToolbarRenderer::MenuDropdownPaintBounds(ResolveOpenMenu(menuBar, interaction));
    if (EmptyRect(paintBounds)) {
        Hide();
        return;
    }

    menuBar_ = menuBar;
    paintBounds_ = paintBounds;
    theme_ = theme;
    interaction_ = &interaction;
    const RenderedState state = Capture(interaction);
    const bool placed = popup_.ShowAt(EditorOverlayPopupWindow::OwnerClientToScreen(owner, paintBounds_));
    if (placed || state != rendered_) {
        rendered_ = state;
        popup_.Invalidate();
    }
}

void EditorToolbarMenuOverlayWindow::Hide() noexcept {
    popup_.Hide();
    interaction_ = nullptr;
    rendered_ = RenderedState{};
}

EditorToolbarMenuOverlayWindow::RenderedState EditorToolbarMenuOverlayWindow::Capture(const EditorShellInteractionState& interaction) {
    RenderedState state{
        .menu = interaction.OpenMenu(),
        .hoveredRow = interaction.HoveredMenuRow(),
        .rowCount = interaction.MenuRowCount(interaction.OpenMenu()),
    };
    if (state.menu == EditorMenuCommand::Layout) {
        // The Layout menu's rows change while it is open (its delete list), so
        // what they say is part of what is on screen.
        for (const EditorLayoutMenuRow& row : interaction.LayoutMenu().Rows()) {
            state.layoutRows += row.label;
            state.layoutRows += row.active ? '\x01' : '\x02';
            state.layoutRows += row.enabled ? '\x01' : '\x02';
        }
    }
    return state;
}

void EditorToolbarMenuOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    // The strips beside the dropdown's shadow belong to the shadow.
    GdiDrawing::FillRectColor(dc, client, RGB(0, 0, 0));
    if (interaction_ == nullptr) {
        return;
    }
    // The dropdown is laid out in the editor window's coordinates; the popup starts
    // at the dropdown's corner.
    EditorMenuRects menu = ResolveOpenMenu(menuBar_, *interaction_);
    const int dx = static_cast<int>(client.left - paintBounds_.left);
    const int dy = static_cast<int>(client.top - paintBounds_.top);
    OffsetRect(&menu.dropdown, dx, dy);
    for (RECT& row : menu.dropdownRows) {
        OffsetRect(&row, dx, dy);
    }
    EditorToolbarRenderer{}.PaintMenuDropdown(dc, menu, theme_, *interaction_);
}

bool EditorToolbarMenuOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
        popup_.ForwardToOwner(message, wparam, lparam);
        if (interaction_ == nullptr || interaction_->OpenMenu() == EditorMenuCommand::None) {
            Hide();
        } else if (RenderedState state = Capture(*interaction_); state != rendered_) {
            rendered_ = std::move(state);
            popup_.Invalidate();
        }
        return true;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
