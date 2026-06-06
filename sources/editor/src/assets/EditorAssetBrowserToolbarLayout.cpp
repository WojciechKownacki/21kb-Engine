#include "assets/EditorAssetBrowserToolbarLayout.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {

void EditorAssetBrowserToolbarLayout::Apply(EditorAssetBrowserLayoutRects& layout) noexcept {
    constexpr int toolbarGap = 10;
    const int frameWidth = static_cast<int>(layout.frame.right - layout.frame.left);
    const int toolbarButtonTop = layout.toolbar.top + 6;
    const int toolbarButtonBottom = layout.toolbar.bottom - 6;

    layout.newFolderButton = RECT{ layout.toolbar.left + 130, toolbarButtonTop, layout.toolbar.left + 160, toolbarButtonBottom };
    layout.filtersButton = RECT{ layout.newFolderButton.right + toolbarGap, toolbarButtonTop, layout.newFolderButton.right + toolbarGap + 72, toolbarButtonBottom };

    const int preferredSearchWidth = std::clamp(frameWidth / 3, 240, 326);
    const int pathLeft = layout.filtersButton.right + toolbarGap;
    int searchLeft = layout.toolbar.right - 10 - preferredSearchWidth;
    if (searchLeft < pathLeft + 96) {
        searchLeft = std::min(static_cast<int>(layout.toolbar.right) - 10 - 180, pathLeft + 96);
    }
    if (searchLeft < pathLeft) {
        searchLeft = pathLeft;
    }

    layout.search = RECT{ searchLeft, toolbarButtonTop, layout.toolbar.right - 10, toolbarButtonBottom };
    if (layout.search.right < layout.search.left) {
        layout.search.right = layout.search.left;
    }

    layout.path = RECT{ pathLeft, toolbarButtonTop, layout.search.left - toolbarGap, toolbarButtonBottom };
    if (layout.path.right < layout.path.left) {
        layout.path.right = layout.path.left;
    }

    layout.refreshButton = RECT{ layout.toolbar.right, layout.toolbar.top, layout.toolbar.right, layout.toolbar.top };
    layout.renameButton = RECT{ layout.toolbar.right, layout.toolbar.top, layout.toolbar.right, layout.toolbar.top };
    layout.deleteButton = RECT{ layout.toolbar.right, layout.toolbar.top, layout.toolbar.right, layout.toolbar.top };
}

} // namespace kb::editor

#endif
