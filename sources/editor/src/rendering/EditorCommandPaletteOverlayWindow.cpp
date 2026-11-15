#include "rendering/EditorCommandPaletteOverlayWindow.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/components/EditorDialogStyle.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <string_view>

namespace kb::editor {
namespace {

constexpr int kTextSize = 13;
constexpr int kDetailSize = 11;
constexpr int kRowPadding = 10;

[[nodiscard]] COLORREF Ink(const EditorColor& color) noexcept {
    return RGB(color.r, color.g, color.b);
}

[[nodiscard]] RECT Shifted(const EditorPaletteRect& rect, int dx, int dy) noexcept {
    return RECT{ rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy };
}

void DrawLine(HDC dc, RECT rect, std::string_view text, COLORREF color, UINT format) {
    if (rect.right <= rect.left || rect.bottom <= rect.top || text.empty()) {
        return;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextUtf8(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

} // namespace

EditorCommandPaletteOverlayWindow::EditorCommandPaletteOverlayWindow() noexcept
    : popup_(ClassName, *this, CS_DROPSHADOW) {}

void EditorCommandPaletteOverlayWindow::Show(HWND owner, const EditorTheme& theme, const EditorShellInteractionState& interaction) {
    const EditorCommandPaletteModel& palette = interaction.CommandPalette();
    if (owner == nullptr || !palette.IsOpen() || !popup_.Ensure(owner)) {
        Hide();
        return;
    }
    RECT client{};
    GetClientRect(owner, &client);
    layout_ = EditorCommandPaletteLayout::Resolve(
        static_cast<int>(client.right - client.left), static_cast<int>(client.bottom - client.top), palette.VisibleCount());
    theme_ = theme;
    palette_ = &palette;
    const bool placed = popup_.ShowAt(EditorOverlayPopupWindow::OwnerClientToScreen(owner, Shifted(layout_.panel, 0, 0)));
    if (placed || palette.Revision() != paintedRevision_) {
        paintedRevision_ = palette.Revision();
        popup_.Invalidate();
    }
}

void EditorCommandPaletteOverlayWindow::Hide() noexcept {
    popup_.Hide();
    palette_ = nullptr;
    paintedRevision_ = 0;
}

void EditorCommandPaletteOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    if (palette_ == nullptr) {
        return;
    }
    const EditorCommandPaletteModel& palette = *palette_;
    // The layout is in the editor window's coordinates; the popup starts at the panel's corner.
    const int dx = static_cast<int>(client.left) - layout_.panel.left;
    const int dy = static_cast<int>(client.top) - layout_.panel.top;
    const COLORREF ink = Ink(theme_.textPrimary);
    const COLORREF muted = Ink(theme_.textSecondary);
    const COLORREF accent = Ink(theme_.accent);
    GdiDrawing::DrawSharpFrame(dc, client, Ink(theme_.header), Ink(theme_.borderPanel));

    const ScopedFont textFont(kTextSize, FW_NORMAL);
    const ScopedFont detailFont(kDetailSize, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, textFont.handle);

    // The text box: a magnifier, what is typed (or what to type) and a caret.
    const RECT field = Shifted(layout_.field, dx, dy);
    GdiDrawing::DrawSharpFrame(dc, field, Ink(theme_.chrome), accent);
    constexpr int kIcon = 16;
    const int iconTop = static_cast<int>(field.top) + ((static_cast<int>(field.bottom - field.top) - kIcon) / 2);
    HeroIconPainter::Draw(dc, RECT{ field.left + kRowPadding, iconTop, field.left + kRowPadding + kIcon, iconTop + kIcon }, HeroIconKind::MagnifyingGlass, muted, 1);
    const int textLeft = static_cast<int>(field.left) + kRowPadding + kIcon + 8;
    const RECT textArea{ textLeft, field.top, field.right - kRowPadding, field.bottom };
    const std::string_view typed = palette.Field().Query();
    if (typed.empty()) {
        DrawLine(dc, textArea, EditorText("palette.placeholder", "Find an asset or scene, or start with > for commands"), Ink(theme_.textDisabled), DT_LEFT);
    } else {
        DrawLine(dc, textArea, typed, ink, DT_LEFT);
    }
    const int caretLeft = std::min(textLeft + EditorDialogStyle::TextWidth(dc, typed, kTextSize) + 1, static_cast<int>(field.right) - kRowPadding);
    GdiDrawing::FillRectColor(dc, RECT{ caretLeft, field.top + 8, caretLeft + 1, field.bottom - 8 }, ink);

    // The rows on screen, the chosen one marked.
    const bool commands = palette.CurrentMode() == EditorCommandPaletteModel::Mode::Commands;
    if (layout_.rowCount == 0) {
        const bool nothingTypedYet = !commands && typed.empty();
        if (!nothingTypedYet) {
            DrawLine(dc, RECT{ layout_.list.left + dx + kRowPadding, layout_.list.top + dy, layout_.list.right + dx - kRowPadding, layout_.list.top + dy + 28 },
                EditorText("palette.noMatches", "Nothing found"), muted, DT_LEFT);
        }
    }
    for (int slot = 0; slot < layout_.rowCount; ++slot) {
        const int index = palette.FirstVisible() + slot;
        const EditorCommandPaletteModel::Row row = palette.ResultAt(static_cast<std::size_t>(index));
        const RECT rect = Shifted(layout_.rows[static_cast<std::size_t>(slot)], dx, dy);
        const bool chosen = index == palette.Selected();
        if (chosen) {
            GdiDrawing::FillRectColor(dc, rect, Ink(theme_.toolbarButton));
            GdiDrawing::FillRectColor(dc, RECT{ rect.left, rect.top + 2, rect.left + 3, rect.bottom - 2 }, accent);
        }
        int right = static_cast<int>(rect.right) - kRowPadding;
        if (!row.shortcut.empty()) {
            const int width = EditorDialogStyle::TextWidth(dc, row.shortcut, kTextSize);
            DrawLine(dc, RECT{ right - width, rect.top, right, rect.bottom }, row.shortcut, muted, DT_RIGHT);
            right -= width + 12;
        }
        if (!row.detail.empty()) {
            const ScopedGdiObject smallFont(dc, detailFont.handle);
            const int width = std::min(EditorDialogStyle::TextWidth(dc, row.detail, kDetailSize), (right - static_cast<int>(rect.left)) * 45 / 100);
            DrawLine(dc, RECT{ right - width, rect.top, right, rect.bottom }, row.detail, muted, DT_RIGHT);
            right -= width + 12;
        }
        DrawLine(dc, RECT{ rect.left + kRowPadding, rect.top, right, rect.bottom }, row.label, ink, DT_LEFT);
    }

    const ScopedGdiObject smallFont(dc, detailFont.handle);
    DrawLine(dc, Shifted(layout_.hint, dx, dy),
        commands ? EditorText("palette.hintCommands", "Enter runs the command. Delete the > to find assets and scenes.")
                 : EditorText("palette.hintAssets", "Enter opens it. Start with > to find a command."),
        muted, DT_LEFT);
}

bool EditorCommandPaletteOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
        popup_.ForwardToOwner(message, wparam, lparam);
        return true;
    case WM_MOUSEWHEEL:
        // The wheel's position is a screen position and the owner does not read it.
        if (popup_.Owner() != nullptr) {
            SendMessageW(popup_.Owner(), message, wparam, lparam);
        }
        return true;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
