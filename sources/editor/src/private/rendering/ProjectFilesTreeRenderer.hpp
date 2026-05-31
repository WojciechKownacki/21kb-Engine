#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <vector>

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserState;

class ProjectFilesTreeRenderer {
public:
    ProjectFilesTreeRenderer() = delete;

    static void Paint(
        HDC dc,
        const EditorAssetBrowserLayoutRects& layout,
        const EditorTheme& theme,
        const EditorAssetBrowserState& state,
        const std::vector<EditorAssetFolderRow>& folders);
};

#endif

} // namespace kb::editor
