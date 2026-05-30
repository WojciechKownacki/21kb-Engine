#include "docking/DockLeafPanelOrder.hpp"

#include "docking/DockNodeQuery.hpp"

#include <algorithm>

namespace kb::editor {

void DockLeafPanelOrder::Activate(DockNode* root, std::uint32_t panelId) noexcept {
    if (DockNode* leaf = DockNodeQuery::FindLeafContaining(root, panelId); leaf != nullptr) {
        leaf->activePanelId = panelId;
    }
}

void DockLeafPanelOrder::Reorder(DockNode* root, std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex) {
    DockNode* leaf = DockNodeQuery::FindLeaf(root, leafId);
    if (leaf == nullptr) {
        return;
    }

    auto it = std::find(leaf->panels.begin(), leaf->panels.end(), panelId);
    if (it == leaf->panels.end()) {
        return;
    }

    const std::uint32_t oldIndex = static_cast<std::uint32_t>(std::distance(leaf->panels.begin(), it));
    const std::uint32_t lastIndex = leaf->panels.empty() ? 0U : static_cast<std::uint32_t>(leaf->panels.size() - 1U);
    newIndex = std::min(newIndex, lastIndex);
    if (oldIndex == newIndex) {
        return;
    }

    const std::uint32_t value = *it;
    leaf->panels.erase(it);
    leaf->panels.insert(leaf->panels.begin() + static_cast<std::ptrdiff_t>(newIndex), value);
    leaf->activePanelId = panelId;
}

std::uint32_t DockLeafPanelOrder::Count(const DockNode* root, std::uint32_t leafId) noexcept {
    const DockNode* leaf = DockNodeQuery::FindLeaf(root, leafId);
    return leaf == nullptr ? 0U : static_cast<std::uint32_t>(leaf->panels.size());
}

} // namespace kb::editor
