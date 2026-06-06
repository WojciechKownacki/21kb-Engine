#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserPanelLayoutBuilder {
public:
    EditorAssetBrowserPanelLayoutBuilder() = delete;

    [[nodiscard]] static EditorAssetBrowserLayoutRects Build(const RECT& content) noexcept;
    [[nodiscard]] static EditorAssetBrowserLayoutRects Build(const RECT& content, int treeWidth) noexcept;
};

#endif

} // namespace kb::editor
