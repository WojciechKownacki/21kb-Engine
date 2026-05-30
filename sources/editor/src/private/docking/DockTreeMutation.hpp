#pragma once

#include "docking/DockNode.hpp"
#include "docking/DockNodeIdSource.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockTreeMutation {
public:
    using NextNodeIdFn = DockNextNodeIdFn;

    DockTreeMutation() = delete;

    static void RemovePanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId) noexcept;
    static void PruneEmptyBranches(std::unique_ptr<DockNode>& root) noexcept;
    static void DockPanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, NextNodeIdFn nextNodeId, void* context);
};

} // namespace kb::editor
