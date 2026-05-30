#include "docking/DockNodeQuery.hpp"

#include "docking/DockNodeFinder.hpp"
#include "docking/DockNodeSlotFinder.hpp"

namespace kb::editor {

DockNode* DockNodeQuery::FindNode(DockNode* root, std::uint32_t nodeId) noexcept {
    return DockNodeFinder::FindNode(root, nodeId);
}

const DockNode* DockNodeQuery::FindNode(const DockNode* root, std::uint32_t nodeId) noexcept {
    return DockNodeFinder::FindNode(root, nodeId);
}

DockNode* DockNodeQuery::FindLeaf(DockNode* root, std::uint32_t leafId) noexcept {
    return DockNodeFinder::FindLeaf(root, leafId);
}

const DockNode* DockNodeQuery::FindLeaf(const DockNode* root, std::uint32_t leafId) noexcept {
    return DockNodeFinder::FindLeaf(root, leafId);
}

DockNode* DockNodeQuery::FindLeafContaining(DockNode* root, std::uint32_t panelId) noexcept {
    return DockNodeFinder::FindLeafContaining(root, panelId);
}

const DockNode* DockNodeQuery::FindLeafContaining(const DockNode* root, std::uint32_t panelId) noexcept {
    return DockNodeFinder::FindLeafContaining(root, panelId);
}

std::unique_ptr<DockNode>* DockNodeQuery::FindNodeSlot(std::unique_ptr<DockNode>& root, std::uint32_t nodeId) noexcept {
    return DockNodeSlotFinder::FindNodeSlot(root, nodeId);
}

} // namespace kb::editor
