#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>

namespace kb::editor {

class DockNodeFinder {
public:
    DockNodeFinder() = delete;

    [[nodiscard]] static DockNode* FindNode(DockNode* root, std::uint32_t nodeId) noexcept;
    [[nodiscard]] static const DockNode* FindNode(const DockNode* root, std::uint32_t nodeId) noexcept;
    [[nodiscard]] static DockNode* FindLeaf(DockNode* root, std::uint32_t leafId) noexcept;
    [[nodiscard]] static const DockNode* FindLeaf(const DockNode* root, std::uint32_t leafId) noexcept;
    [[nodiscard]] static DockNode* FindLeafContaining(DockNode* root, std::uint32_t panelId) noexcept;
    [[nodiscard]] static const DockNode* FindLeafContaining(const DockNode* root, std::uint32_t panelId) noexcept;
};

} // namespace kb::editor
