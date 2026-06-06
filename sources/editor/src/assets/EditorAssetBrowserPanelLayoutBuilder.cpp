#include "assets/EditorAssetBrowserPanelLayoutBuilder.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserBottomBarLayout.hpp"
#include "assets/EditorAssetBrowserToolbarLayout.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] RECT BuildFrame(const RECT& content) noexcept {
    RECT frame{ content.left, content.top, content.right, content.bottom };
    if (frame.right < frame.left) {
        frame.right = frame.left;
    }
    if (frame.bottom < frame.top) {
        frame.bottom = frame.top;
    }
    return frame;
}

[[nodiscard]] int DefaultTreeWidth(const EditorAssetBrowserLayoutRects& layout) noexcept {
    const int frameWidth = static_cast<int>(layout.frame.right - layout.frame.left);
    return std::clamp(frameWidth / 5, 220, 300);
}

void ApplyBody(EditorAssetBrowserLayoutRects& layout, int requestedTreeWidth) noexcept {
    constexpr int splitterWidth = 5;
    const int frameWidth = static_cast<int>(layout.frame.right - layout.frame.left);
    const int maxTreeWidth = std::max(160, frameWidth - 220);
    const int preferredTreeWidth = requestedTreeWidth > 0 ? requestedTreeWidth : DefaultTreeWidth(layout);
    const int treeWidth = std::clamp(preferredTreeWidth, 160, maxTreeWidth);
    const int splitX = layout.frame.left + treeWidth;

    layout.tree = RECT{ layout.frame.left, layout.frame.top, splitX, layout.bottomBar.top };
    layout.treeSplitter = RECT{ splitX - splitterWidth / 2, layout.frame.top, splitX + (splitterWidth + 1) / 2, layout.bottomBar.top };
    layout.toolbar = RECT{ splitX, layout.frame.top, layout.frame.right, layout.frame.top + 34 };
    layout.assetView = RECT{ splitX, layout.toolbar.bottom, layout.frame.right, layout.bottomBar.top };
    if (layout.assetView.right < layout.assetView.left) {
        layout.assetView.right = layout.assetView.left;
    }
}

} // namespace

EditorAssetBrowserLayoutRects EditorAssetBrowserPanelLayoutBuilder::Build(const RECT& content) noexcept {
    EditorAssetBrowserLayoutRects layout{};
    layout.frame = BuildFrame(content);
    layout.bottomBar = RECT{ layout.frame.left, layout.frame.bottom - 26, layout.frame.right, layout.frame.bottom };

    ApplyBody(layout, DefaultTreeWidth(layout));
    EditorAssetBrowserToolbarLayout::Apply(layout);
    EditorAssetBrowserBottomBarLayout::Apply(layout);
    return layout;
}

EditorAssetBrowserLayoutRects EditorAssetBrowserPanelLayoutBuilder::Build(const RECT& content, int treeWidth) noexcept {
    EditorAssetBrowserLayoutRects layout{};
    layout.frame = BuildFrame(content);
    layout.bottomBar = RECT{ layout.frame.left, layout.frame.bottom - 26, layout.frame.right, layout.frame.bottom };

    ApplyBody(layout, treeWidth);
    EditorAssetBrowserToolbarLayout::Apply(layout);
    EditorAssetBrowserBottomBarLayout::Apply(layout);
    return layout;
}

} // namespace kb::editor

#endif
