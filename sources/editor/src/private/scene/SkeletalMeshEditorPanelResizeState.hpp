#pragma once

#include <algorithm>
#include <cstdint>

namespace kb::editor {

enum class SkeletalMeshEditorPanelDrag : std::uint8_t {
    None,
    ToolboxWidth,
    SkeletonTreeWidth,
    TreeDetailsHeight,
};

class SkeletalMeshEditorPanelResizeState {
public:
    [[nodiscard]] int ToolboxWidth() const noexcept { return toolboxWidth_; }
    [[nodiscard]] int SkeletonTreeWidth() const noexcept { return skeletonTreeWidth_; }
    [[nodiscard]] int SkeletonTreeHeight() const noexcept { return skeletonTreeHeight_; }

    void SetToolboxWidth(int width) noexcept { toolboxWidth_ = std::max(0, width); }
    void SetSkeletonTreeWidth(int width) noexcept { skeletonTreeWidth_ = std::max(0, width); }
    void SetSkeletonTreeHeight(int height) noexcept { skeletonTreeHeight_ = std::max(0, height); }

    void BeginDrag(SkeletalMeshEditorPanelDrag drag) noexcept { activeDrag_ = drag; }
    void EndDrag() noexcept { activeDrag_ = SkeletalMeshEditorPanelDrag::None; }
    [[nodiscard]] bool IsDragging(SkeletalMeshEditorPanelDrag drag) const noexcept {
        return activeDrag_ == drag;
    }

private:
    int toolboxWidth_ = 0;
    int skeletonTreeWidth_ = 0;
    int skeletonTreeHeight_ = 0;
    SkeletalMeshEditorPanelDrag activeDrag_ = SkeletalMeshEditorPanelDrag::None;
};

} // namespace kb::editor
