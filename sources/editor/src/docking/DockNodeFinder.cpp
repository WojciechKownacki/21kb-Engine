#include "docking/DockNodeFinder.hpp"

#include <algorithm>

namespace kb::editor {

DockNode* DockNodeFinder::FindNode(DockNode* root, std::uint32_t nodeId) noexcept {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->id == nodeId) {
        return root;
    }
    if (DockNode* found = FindNode(root->first.get(), nodeId); found != nullptr) {
        return found;
    }
    return FindNode(root->second.get(), nodeId);
}

const DockNode* DockNodeFinder::FindNode(const DockNode* root, std::uint32_t nodeId) noexcept {
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

DockNode* DockNodeFinder::FindLeaf(DockNode* root, std::uint32_t leafId) noexcept {
    DockNode* node = FindNode(root, leafId);
    return node != nullptr && node->kind == DockNode::Kind::Leaf ? node : nullptr;
}

const DockNode* DockNodeFinder::FindLeaf(const DockNode* root, std::uint32_t leafId) noexcept {
    const DockNode* node = FindNode(root, leafId);
    return node != nullptr && node->kind == DockNode::Kind::Leaf ? node : nullptr;
}

DockNode* DockNodeFinder::FindLeafContaining(DockNode* root, std::uint32_t panelId) noexcept {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->kind == DockNode::Kind::Leaf && std::find(root->panels.begin(), root->panels.end(), panelId) != root->panels.end()) {
        return root;
    }
    if (DockNode* found = FindLeafContaining(root->first.get(), panelId); found != nullptr) {
        return found;
    }
    return FindLeafContaining(root->second.get(), panelId);
}

const DockNode* DockNodeFinder::FindLeafContaining(const DockNode* root, std::uint32_t panelId) noexcept {
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

} // namespace kb::editor
