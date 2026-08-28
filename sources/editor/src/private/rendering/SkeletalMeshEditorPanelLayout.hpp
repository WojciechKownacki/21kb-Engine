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
    RECT toolboxSplitter{};
    RECT viewport{};
    RECT skeletonTreeSplitter{};
    RECT skeletonTree{};
    RECT treeDetailsSplitter{};
    RECT assetDetails{};
};

class SkeletalMeshEditorPanelLayoutResolver {
public:
    [[nodiscard]] static SkeletalMeshEditorPanelLayout Resolve(
        const RECT& content,
        int requestedToolboxWidth = 0,
        int requestedSkeletonTreeWidth = 0,
        int requestedSkeletonTreeHeight = 0) noexcept {
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
        // The Skeleton Tree needs enough horizontal room for deeply nested rig names.
        // Keep the toolbox compact and dedicate a wider, independently-sized right column to the
        // tree/details stack. On narrow windows preserve a usable viewport before shrinking either
        // sidebar.
        constexpr int minimumViewportWidth = 280;
        constexpr int minimumToolboxWidth = 120;
        constexpr int minimumSkeletonTreeWidth = 180;
        int toolboxWidth = requestedToolboxWidth > 0
            ? requestedToolboxWidth
            : std::clamp(width / 5, 160, 240);
        int skeletonTreeWidth = requestedSkeletonTreeWidth > 0
            ? requestedSkeletonTreeWidth
            : std::clamp(width / 3, 300, 420);
        const int sidebarBudget = std::max(0, width - minimumViewportWidth);
        if (sidebarBudget >= minimumToolboxWidth + minimumSkeletonTreeWidth) {
            skeletonTreeWidth = std::clamp(
                skeletonTreeWidth,
                minimumSkeletonTreeWidth,
                sidebarBudget - minimumToolboxWidth);
            toolboxWidth = std::clamp(
                toolboxWidth,
                minimumToolboxWidth,
                sidebarBudget - skeletonTreeWidth);
        } else if (toolboxWidth + skeletonTreeWidth > sidebarBudget) {
            toolboxWidth = std::min(toolboxWidth, sidebarBudget / 2);
            skeletonTreeWidth = std::max(0, sidebarBudget - toolboxWidth);
        }
        const int viewportWidth = std::max(0, width - toolboxWidth - skeletonTreeWidth);
        constexpr int minimumSkeletonTreeHeight = 120;
        constexpr int minimumAssetDetailsHeight = 120;
        int skeletonTreeHeight = requestedSkeletonTreeHeight > 0
            ? requestedSkeletonTreeHeight
            : (workspaceHeight * 3) / 5;
        if (workspaceHeight >= minimumSkeletonTreeHeight + minimumAssetDetailsHeight) {
            skeletonTreeHeight = std::clamp(
                skeletonTreeHeight,
                minimumSkeletonTreeHeight,
                workspaceHeight - minimumAssetDetailsHeight);
        } else {
            // Both minima cannot fit. Preserve a usable share for each panel instead of allowing
            // either stack to disappear, while retaining the requested pixel height for later
            // window growth in the session state.
            skeletonTreeHeight = workspaceHeight / 2;
        }
        const LONG toolboxBoundary = content.left + toolboxWidth;
        const LONG skeletonTreeBoundary = content.right - skeletonTreeWidth;
        const LONG treeDetailsBoundary = workspaceTop + skeletonTreeHeight;
        return SkeletalMeshEditorPanelLayout{
            .documentBar = { content.left, content.top, content.right, documentBarBottom },
            .meshDocument = { buttonsLeft, buttonsTop, buttonsLeft + documentWidth, buttonsBottom },
            .skeletonDocument = { buttonsLeft + documentWidth, buttonsTop, buttonsRight, buttonsBottom },
            .commandBar = { content.left, documentBarBottom, content.right, workspaceTop },
            .toolbox = { content.left, workspaceTop, toolboxBoundary, content.bottom },
            .toolboxSplitter = { toolboxBoundary - 3, workspaceTop, toolboxBoundary + 4, content.bottom },
            .viewport = { toolboxBoundary, workspaceTop, toolboxBoundary + viewportWidth, content.bottom },
            .skeletonTreeSplitter = { skeletonTreeBoundary - 3, workspaceTop, skeletonTreeBoundary + 4, content.bottom },
            .skeletonTree = { skeletonTreeBoundary, workspaceTop, content.right, treeDetailsBoundary },
            .treeDetailsSplitter = {
                skeletonTreeBoundary,
                std::max(static_cast<LONG>(workspaceTop), treeDetailsBoundary - 3),
                content.right,
                std::min(content.bottom, treeDetailsBoundary + 4),
            },
            .assetDetails = { skeletonTreeBoundary, treeDetailsBoundary, content.right, content.bottom },
        };
    }

    [[nodiscard]] static int SkeletonTreeHeightFromPointer(
        const RECT& content, int pointerY) noexcept {
        constexpr int documentBarHeight = 38;
        constexpr int commandBarHeight = 36;
        const int workspaceTop = std::min(
            static_cast<int>(content.bottom),
            static_cast<int>(content.top) + documentBarHeight + commandBarHeight);
        return std::max(0, pointerY - workspaceTop);
    }
};
#endif

} // namespace kb::editor
