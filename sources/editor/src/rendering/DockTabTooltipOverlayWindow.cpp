#include "rendering/DockTabTooltipOverlayWindow.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/EditorTextMeasure.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr int kTooltipFontSize = 12;
constexpr int kTooltipPaddingX = 10;
constexpr int kTooltipHeight = 28;
constexpr int kTooltipGapBelowTab = 4;
constexpr int kTooltipEdgeInset = 8;

} // namespace

DockTabTooltipOverlayWindow::DockTabTooltipOverlayWindow() noexcept
    : popup_(ClassName, *this) {}

void DockTabTooltipOverlayWindow::Show(HWND owner, const RECT& tab, std::string_view title, const EditorTheme& theme) {
    if (owner == nullptr || title.empty() || !popup_.Ensure(owner)) {
        Hide();
        return;
    }

    RECT client{};
    GetClientRect(owner, &client);
    const int room = std::max(1, static_cast<int>(client.right - client.left) - (2 * kTooltipEdgeInset));
    const int width = std::min(room, MeasureTextWidth(title, kTooltipFontSize) + (2 * kTooltipPaddingX));
    const int left = std::clamp(static_cast<int>(tab.left), kTooltipEdgeInset, std::max(kTooltipEdgeInset, static_cast<int>(client.right) - kTooltipEdgeInset - width));
    const int top = static_cast<int>(tab.bottom) + kTooltipGapBelowTab;

    theme_ = theme;
    const bool textChanged = title_ != title;
    title_ = std::string{ title };
    const bool placed = popup_.ShowAt(EditorOverlayPopupWindow::OwnerClientToScreen(owner, RECT{ left, top, left + width, top + kTooltipHeight }));
    if (placed || textChanged) {
        popup_.Invalidate();
    }
}

void DockTabTooltipOverlayWindow::Hide() noexcept {
    popup_.Hide();
    title_.clear();
}

void DockTabTooltipOverlayWindow::PaintOverlay(HDC dc, const RECT& client) {
    GdiDrawing::DrawSharpFrame(dc, client, GdiDrawing::ToColorRef(theme_.chrome), GdiDrawing::ToColorRef(theme_.borderPanel));
    GdiDrawing::DrawCenteredText(dc, client, title_, GdiDrawing::ToColorRef(theme_.textPrimary));
}

bool DockTabTooltipOverlayWindow::HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
        popup_.ForwardToOwner(message, wparam, lparam);
        return true;
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
