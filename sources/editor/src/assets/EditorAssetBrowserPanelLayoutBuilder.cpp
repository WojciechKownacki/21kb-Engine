#include "assets/EditorAssetBrowserPanelLayoutBuilder.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserBottomBarLayout.hpp"
#include "assets/EditorAssetBrowserToolbarLayout.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] RECT BuildFrame(const RECT& content) noexcept {
    RECT frame{ content.left + 8, content.top + 8, content.right - 8, content.bottom - 8 };
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
    const int treeWidth = std::clamp(frameWidth / 5, 180, 280);

    RECT body{ layout.frame.left, layout.toolbar.bottom + 8, layout.frame.right, layout.bottomBar.top - 8 };
    layout.tree = RECT{ body.left, body.top, body.left + treeWidth, body.bottom };
    layout.assetView = RECT{ layout.tree.right + 8, body.top, body.right, body.bottom };
    if (layout.assetView.right < layout.assetView.left) {
        layout.assetView.right = layout.assetView.left;
    }
}

} // namespace

EditorAssetBrowserLayoutRects EditorAssetBrowserPanelLayoutBuilder::Build(const RECT& content) noexcept {
    EditorAssetBrowserLayoutRects layout{};
    layout.frame = BuildFrame(content);
    layout.toolbar = RECT{ layout.frame.left, layout.frame.top, layout.frame.right, layout.frame.top + 36 };
    layout.bottomBar = RECT{ layout.frame.left, layout.frame.bottom - 30, layout.frame.right, layout.frame.bottom };

    ApplyBody(layout);
    EditorAssetBrowserToolbarLayout::Apply(layout);
    EditorAssetBrowserBottomBarLayout::Apply(layout);
    return layout;
}

} // namespace kb::editor

#endif
