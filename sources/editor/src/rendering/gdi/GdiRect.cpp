#include "rendering/gdi/GdiRect.hpp"

#if defined(_WIN32)

namespace kb::editor {

RECT GdiRect::Inset(RECT rect, int amount) noexcept {
    rect.left += amount;
    rect.top += amount;
    rect.right -= amount;
    rect.bottom -= amount;
    return rect;
}

RECT GdiRect::FromDockRect(const DockRect& rect) noexcept {
    return RECT{ rect.x, rect.y, rect.x + rect.width, rect.y + rect.height };
}

} // namespace kb::editor

#endif
