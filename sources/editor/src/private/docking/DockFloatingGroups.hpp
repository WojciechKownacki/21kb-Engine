#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>
#include <vector>

namespace kb::editor {

// The tab groups of the floating windows. Each window holds one leaf of its own, numbered from
// the same counter as the leaves of the docked tree, so a leaf id names a group in either.
class DockFloatingGroups {
public:
    DockFloatingGroups() = delete;

    [[nodiscard]] static DockNode* Find(std::vector<DockNode>& groups, std::uint32_t groupId) noexcept;
    [[nodiscard]] static const DockNode* Find(const std::vector<DockNode>& groups, std::uint32_t groupId) noexcept;
    [[nodiscard]] static DockNode* FindContaining(std::vector<DockNode>& groups, std::uint32_t panelId) noexcept;
    [[nodiscard]] static const DockNode* FindContaining(const std::vector<DockNode>& groups, std::uint32_t panelId) noexcept;

    // Takes the panel out of its window's group; a group left without tabs goes with it.
    static void RemovePanel(std::vector<DockNode>& groups, std::uint32_t panelId);
};

} // namespace kb::editor
