#include "docking/DockNodeQuery.hpp"

#include <algorithm>

namespace kb::editor {

DockNode* DockNodeQuery::FindNode(DockNode* root, std::uint32_t nodeId) noexcept {
    return const_cast<DockNode*>(FindNode(static_cast<const DockNode*>(root), nodeId));
}

const DockNode* DockNodeQuery::FindNode(const DockNode* root, std::uint32_t nodeId) noexcept {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->id == nodeId) {
        return root;
    }
    if (const DockNode* found = FindNode(root->first.get(), nodeId); found != nullptr) {
        return found;
    }
    return FindNode(root->second.get(), nodeId);
}

DockNode* DockNodeQuery::FindLeaf(DockNode* root, std::uint32_t leafId) noexcept {
    DockNode* node = FindNode(root, leafId);
    return node != nullptr && node->kind == DockNode::Kind::Leaf ? node : nullptr;
}

const DockNode* DockNodeQuery::FindLeaf(const DockNode* root, std::uint32_t leafId) noexcept {
    const DockNode* node = FindNode(root, leafId);
    return node != nullptr && node->kind == DockNode::Kind::Leaf ? node : nullptr;
}

DockNode* DockNodeQuery::FindLeafContaining(DockNode* root, std::uint32_t panelId) noexcept {
    return const_cast<DockNode*>(FindLeafContaining(static_cast<const DockNode*>(root), panelId));
}

const DockNode* DockNodeQuery::FindLeafContaining(const DockNode* root, std::uint32_t panelId) noexcept {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->kind == DockNode::Kind::Leaf && std::find(root->panels.begin(), root->panels.end(), panelId) != root->panels.end()) {
        return root;
    }
    if (const DockNode* found = FindLeafContaining(root->first.get(), panelId); found != nullptr) {
        return found;
    }
    return FindLeafContaining(root->second.get(), panelId);
}

std::unique_ptr<DockNode>* DockNodeQuery::FindNodeSlot(std::unique_ptr<DockNode>& root, std::uint32_t nodeId) noexcept {
    return root != nullptr && root->id == nodeId ? &root : FindNodeSlotRecursive(root, nodeId);
}

std::unique_ptr<DockNode>* DockNodeQuery::FindNodeSlotRecursive(std::unique_ptr<DockNode>& node, std::uint32_t nodeId) noexcept {
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
