#include "assets/EditorAssetBrowserPanelLayoutBuilder.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserBottomBarLayout.hpp"
#include "assets/EditorAssetBrowserToolbarLayout.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] RECT BuildFrame(const RECT& content) noexcept {
    RECT frame{ content.left + 2, content.top + 4, content.right - 2, content.bottom - 4 };
    if (frame.right < frame.left) {
        frame.right = frame.left;
    }
    if (frame.bottom < frame.top) {
        frame.bottom = frame.top;
    }
    return frame;
}

void ApplyBody(EditorAssetBrowserLayoutRects& layout) noexcept {
    const int frameWidth = static_cast<int>(layout.frame.right - layout.frame.left);
    const int treeWidth = std::clamp(frameWidth / 5, 220, 300);

    RECT body{ layout.frame.left, layout.toolbar.bottom + 6, layout.frame.right, layout.bottomBar.top - 6 };
    layout.tree = RECT{ body.left, body.top, body.left + treeWidth, body.bottom };
    layout.assetView = RECT{ layout.tree.right + 6, body.top, body.right, body.bottom };
    if (layout.assetView.right < layout.assetView.left) {
        layout.assetView.right = layout.assetView.left;
    }
}

} // namespace

EditorAssetBrowserLayoutRects EditorAssetBrowserPanelLayoutBuilder::Build(const RECT& content) noexcept {
    EditorAssetBrowserLayoutRects layout{};
    layout.frame = BuildFrame(content);
    layout.toolbar = RECT{ layout.frame.left, layout.frame.top, layout.frame.right, layout.frame.top + 34 };
    layout.bottomBar = RECT{ layout.frame.left, layout.frame.bottom - 26, layout.frame.right, layout.frame.bottom };

    ApplyBody(layout);
    EditorAssetBrowserToolbarLayout::Apply(layout);
    EditorAssetBrowserBottomBarLayout::Apply(layout);
    return layout;
}

} // namespace kb::editor

#endif
