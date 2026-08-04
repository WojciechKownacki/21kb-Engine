#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)
struct SkeletalMeshEditorPanelLayout {
    RECT toolbox{};
    RECT viewport{};
    RECT skeletonTree{};
    RECT assetDetails{};
};

class SkeletalMeshEditorPanelLayoutResolver {
public:
    [[nodiscard]] static SkeletalMeshEditorPanelLayout Resolve(const RECT& content) noexcept {
        const int width = content.right > content.left ? content.right - content.left : 0;
        const int height = content.bottom > content.top ? content.bottom - content.top : 0;
        const int sideWidth = width / 5;
        const int viewportWidth = width - 2 * sideWidth;
        const int skeletonTreeHeight = (height * 3) / 5;
        return SkeletalMeshEditorPanelLayout{
            .toolbox = { content.left, content.top, content.left + sideWidth, content.bottom },
            .viewport = { content.left + sideWidth, content.top, content.left + sideWidth + viewportWidth, content.bottom },
            .skeletonTree = { content.left + sideWidth + viewportWidth, content.top, content.right, content.top + skeletonTreeHeight },
            .assetDetails = { content.left + sideWidth + viewportWidth, content.top + skeletonTreeHeight, content.right, content.bottom },
        };
    }
};
#endif

} // namespace kb::editor
