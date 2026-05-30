#include "docking/DockNodeSlotFinder.hpp"

namespace kb::editor {

std::unique_ptr<DockNode>* DockNodeSlotFinder::FindNodeSlot(std::unique_ptr<DockNode>& root, std::uint32_t nodeId) noexcept {
    return root != nullptr && root->id == nodeId ? &root : FindNodeSlotRecursive(root, nodeId);
}

std::unique_ptr<DockNode>* DockNodeSlotFinder::FindNodeSlotRecursive(std::unique_ptr<DockNode>& node, std::uint32_t nodeId) noexcept {
    if (node == nullptr || node->kind != DockNode::Kind::Split) {
        return nullptr;
    }
    if (node->first != nullptr && node->first->id == nodeId) {
        return &node->first;
    }
    if (node->second != nullptr && node->second->id == nodeId) {
        return &node->second;
    }
    if (std::unique_ptr<DockNode>* slot = FindNodeSlotRecursive(node->first, nodeId); slot != nullptr) {
        return slot;
    }
    return FindNodeSlotRecursive(node->second, nodeId);
}

} // namespace kb::editor
