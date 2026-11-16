#include "docking/DockFloatingGroups.hpp"

#include <algorithm>

namespace kb::editor {

DockNode* DockFloatingGroups::Find(std::vector<DockNode>& groups, std::uint32_t groupId) noexcept {
    const auto entry = std::ranges::find(groups, groupId, &DockNode::id);
    return entry == groups.end() ? nullptr : &*entry;
}

const DockNode* DockFloatingGroups::Find(const std::vector<DockNode>& groups, std::uint32_t groupId) noexcept {
    const auto entry = std::ranges::find(groups, groupId, &DockNode::id);
    return entry == groups.end() ? nullptr : &*entry;
}

DockNode* DockFloatingGroups::FindContaining(std::vector<DockNode>& groups, std::uint32_t panelId) noexcept {
    for (DockNode& group : groups) {
        if (std::ranges::find(group.panels, panelId) != group.panels.end()) {
            return &group;
        }
    }
    return nullptr;
}

const DockNode* DockFloatingGroups::FindContaining(const std::vector<DockNode>& groups, std::uint32_t panelId) noexcept {
    for (const DockNode& group : groups) {
        if (std::ranges::find(group.panels, panelId) != group.panels.end()) {
            return &group;
        }
    }
    return nullptr;
}

void DockFloatingGroups::RemovePanel(std::vector<DockNode>& groups, std::uint32_t panelId) {
    DockNode* group = FindContaining(groups, panelId);
    if (group == nullptr) {
        return;
    }
    std::erase(group->panels, panelId);
    if (group->panels.empty()) {
        groups.erase(groups.begin() + (group - groups.data()));
        return;
    }
    if (group->activePanelId == panelId) {
        group->activePanelId = group->panels.front();
    }
}

} // namespace kb::editor
