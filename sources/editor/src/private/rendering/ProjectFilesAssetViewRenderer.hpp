#pragma once

#include "assets/EditorAssetBrowserLayout.hpp"
#include "assets/EditorAssetUsers.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#include <vector>

namespace kb::assets {
class AssetManager;
}

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetBrowserState;
class EditorMeshThumbnailService;

class ProjectFilesAssetViewRenderer {
public:
    ProjectFilesAssetViewRenderer() = delete;

    static void Paint(
        HDC dc,
        const EditorAssetBrowserLayoutRects& layout,
        const EditorTheme& theme,
        const EditorAssetBrowserState& state,
        kb::assets::AssetManager& manager,
        EditorMeshThumbnailService& meshThumbnails,
        const std::vector<EditorAssetFolderRow>& folders,
        const std::vector<EditorAssetItemRow>& assets,
        const EditorAssetUsers& users);
};

#endif

} // namespace kb::editor
