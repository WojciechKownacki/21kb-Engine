#include "rendering/FloatingWindowControlRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "windowing/FloatingWindowControlLayout.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<HeroIconKind> IconForControl(FloatingWindowControlKind control) noexcept {
    switch (control) {
    case FloatingWindowControlKind::Minimize:
        return std::optional<HeroIconKind>{ HeroIconKind::Minus };
    case FloatingWindowControlKind::MaximizeRestore:
        return std::optional<HeroIconKind>{ HeroIconKind::Stop };
    case FloatingWindowControlKind::Close:
        return std::optional<HeroIconKind>{ HeroIconKind::XMark };
    case FloatingWindowControlKind::None:
    default:
        return std::nullopt;
    }
}

[[nodiscard]] RECT IconRectForControl(const DockRect& control) noexcept {
    constexpr int iconSize = 12;
    const int x = control.x + ((control.width - iconSize) / 2);
    const int y = control.y + ((control.height - iconSize) / 2);
    return RECT{ x, y, x + iconSize, y + iconSize };
}

} // namespace

void FloatingWindowControlRenderer::Paint(HDC dc, const RECT& client, const EditorTheme& theme, const EditorMetrics& metrics) const {
    const COLORREF iconColor = GdiDrawing::ToColorRef(theme.textSecondary);
    const int clientWidth = client.right - client.left;
    for (FloatingWindowControlKind control : FloatingWindowControlLayout::OrderedControls) {
        const std::optional<HeroIconKind> icon = IconForControl(control);
        if (!icon.has_value()) {
            continue;
        }
        const DockRect controlRect = FloatingWindowControlLayout::Rect(metrics, clientWidth, control);
        HeroIconPainter::Draw(dc, IconRectForControl(controlRect), *icon, iconColor);
    }
}

} // namespace kb::editor

#endif
