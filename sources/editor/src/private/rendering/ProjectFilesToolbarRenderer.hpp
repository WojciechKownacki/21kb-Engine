#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserState;

class ProjectFilesToolbarRenderer {
public:
    ProjectFilesToolbarRenderer() = delete;

    static void Paint(HDC dc, const EditorAssetBrowserLayoutRects& layout, const EditorTheme& theme, const EditorAssetBrowserState& state);
};

#endif

} // namespace kb::editor
