#include "docking/DockPanelRemover.hpp"

#include "docking/DockNodeQuery.hpp"

#include <algorithm>

namespace kb::editor {

void DockPanelRemover::RemovePanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId) noexcept {
    DockNode* leaf = DockNodeQuery::FindLeafContaining(root.get(), panelId);
    if (leaf == nullptr) {
        return;
    }

    leaf->panels.erase(std::remove(leaf->panels.begin(), leaf->panels.end(), panelId), leaf->panels.end());
    if (leaf->activePanelId == panelId) {
        leaf->activePanelId = leaf->panels.empty() ? 0 : leaf->panels.front();
    }
}

} // namespace kb::editor
