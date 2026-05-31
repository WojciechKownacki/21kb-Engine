#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserToolbarLayout {
public:
    EditorAssetBrowserToolbarLayout() = delete;

    static void Apply(EditorAssetBrowserLayoutRects& layout) noexcept;
};

#endif

} // namespace kb::editor
