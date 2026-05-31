#include "assets/EditorAssetBrowserContextMenuLayout.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {

RECT EditorAssetBrowserContextMenuLayout::ContextMenuRect(const RECT& content, int x, int y, int itemCount) noexcept {
    const int separators = std::max(0, itemCount - 1);
    const int width = EditorAssetBrowserLayout::ContextMenuWidth;
    const int height = EditorAssetBrowserLayout::ContextMenuPadding * 2
        + itemCount * EditorAssetBrowserLayout::ContextMenuRowHeight
        + separators * EditorAssetBrowserLayout::ContextMenuSeparatorHeight;
    const int contentLeft = static_cast<int>(content.left);
    const int contentTop = static_cast<int>(content.top);
    const int contentRight = static_cast<int>(content.right);
    const int contentBottom = static_cast<int>(content.bottom);
    x = std::clamp(x, contentLeft, std::max(contentLeft, contentRight - width));
    y = std::clamp(y, contentTop, std::max(contentTop, contentBottom - height));
    return RECT{ x, y, x + width, y + height };
}

RECT EditorAssetBrowserContextMenuLayout::ContextMenuItemRect(const RECT& menu, int index) noexcept {
    int top = menu.top + EditorAssetBrowserLayout::ContextMenuPadding;
    for (int row = 0; row < index; ++row) {
        top += EditorAssetBrowserLayout::ContextMenuRowHeight + EditorAssetBrowserLayout::ContextMenuSeparatorHeight;
    }
    return RECT{ menu.left + EditorAssetBrowserLayout::ContextMenuPadding, top, menu.right - EditorAssetBrowserLayout::ContextMenuPadding, top + EditorAssetBrowserLayout::ContextMenuRowHeight };
}

} // namespace kb::editor

#endif
