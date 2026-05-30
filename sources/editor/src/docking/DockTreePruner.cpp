#include "docking/DockTreePruner.hpp"

namespace kb::editor {

void DockTreePruner::PruneEmptyBranches(std::unique_ptr<DockNode>& root) noexcept {
    if (root != nullptr && PruneRecursive(root)) {
        root.reset();
    }
}

bool DockTreePruner::PruneRecursive(std::unique_ptr<DockNode>& node) noexcept {
    if (node == nullptr) {
        return true;
    }
    if (node->kind == DockNode::Kind::Leaf) {
        return node->panels.empty();
    }

    const bool firstEmpty = PruneRecursive(node->first);
    if (firstEmpty) {
        node->first.reset();
    }
    const bool secondEmpty = PruneRecursive(node->second);
    if (secondEmpty) {
        node->second.reset();
    }

    if (node->first == nullptr && node->second == nullptr) {
        return true;
    }
    if (node->first == nullptr) {
        node = std::move(node->second);
    } else if (node->second == nullptr) {
        node = std::move(node->first);
    }
    return false;
}

} // namespace kb::editor
