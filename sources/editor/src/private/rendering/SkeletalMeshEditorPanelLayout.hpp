#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>

namespace kb::editor {

#if defined(_WIN32)
struct SkeletalMeshEditorPanelLayout {
    RECT documentBar{};
    RECT meshDocument{};
    RECT skeletonDocument{};
    RECT commandBar{};
    RECT toolbox{};
    RECT viewport{};
    RECT skeletonTree{};
    RECT assetDetails{};
};

class SkeletalMeshEditorPanelLayoutResolver {
public:
    [[nodiscard]] static SkeletalMeshEditorPanelLayout Resolve(const RECT& content) noexcept {
        const int width = content.right > content.left ? content.right - content.left : 0;
        constexpr int documentBarHeight = 38;
        constexpr int commandBarHeight = 36;
        const int documentBarBottom = std::min(content.bottom, content.top + documentBarHeight);
        const int workspaceTop = std::min(static_cast<int>(content.bottom), documentBarBottom + commandBarHeight);
        const int workspaceHeight = content.bottom - workspaceTop;
        const LONG buttonsLeft = std::min(content.right, content.left + 112);
        const LONG buttonsRight = std::max(buttonsLeft, content.right - 8);
        const LONG buttonsTop = std::min(documentBarBottom, static_cast<int>(content.top + 5));
        const LONG buttonsBottom = std::max(buttonsTop, static_cast<LONG>(documentBarBottom - 5));
        const int buttonsWidth = static_cast<int>(buttonsRight - buttonsLeft);
        const int documentWidth = buttonsWidth / 2;
        const int sideWidth = width / 5;
        const int viewportWidth = width - 2 * sideWidth;
        const int skeletonTreeHeight = (workspaceHeight * 3) / 5;
        return SkeletalMeshEditorPanelLayout{
            .documentBar = { content.left, content.top, content.right, documentBarBottom },
            .meshDocument = { buttonsLeft, buttonsTop, buttonsLeft + documentWidth, buttonsBottom },
            .skeletonDocument = { buttonsLeft + documentWidth, buttonsTop, buttonsRight, buttonsBottom },
            .commandBar = { content.left, documentBarBottom, content.right, workspaceTop },
            .toolbox = { content.left, workspaceTop, content.left + sideWidth, content.bottom },
            .viewport = { content.left + sideWidth, workspaceTop, content.left + sideWidth + viewportWidth, content.bottom },
            .skeletonTree = { content.left + sideWidth + viewportWidth, workspaceTop, content.right, workspaceTop + skeletonTreeHeight },
            .assetDetails = { content.left + sideWidth + viewportWidth, workspaceTop + skeletonTreeHeight, content.right, content.bottom },
        };
    }
};
#endif

} // namespace kb::editor
