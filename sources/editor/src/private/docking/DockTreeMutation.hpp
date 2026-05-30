#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockTreeMutation {
public:
    using NextNodeIdFn = std::uint32_t (*)(void* context) noexcept;

    DockTreeMutation() = delete;

    static void RemovePanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId) noexcept;
    static void PruneEmptyBranches(std::unique_ptr<DockNode>& root) noexcept;
    static void DockPanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context);

private:
    [[nodiscard]] static std::uint32_t NextNodeId(NextNodeIdFn nextNodeId, void* context) noexcept;
    [[nodiscard]] static DockSplitAxis AxisForZone(DockDropZone zone) noexcept;
    [[nodiscard]] static bool IsDroppedFirst(DockDropZone zone) noexcept;
    [[nodiscard]] static float RatioForTarget(const DockDropPreview& target) noexcept;

    static void DockPanelToCenter(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context);
    static void DockPanelToSplit(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context);
    static void AttachToRoot(std::unique_ptr<DockNode>& root, std::unique_ptr<DockNode> dropped, DockSplitAxis axis, float ratio, bool droppedFirst, NextNodeIdFn nextNodeId, void* context);
};

} // namespace kb::editor
