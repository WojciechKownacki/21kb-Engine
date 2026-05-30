#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockNodeQuery {
public:
    DockNodeQuery() = delete;

    [[nodiscard]] static DockNode* FindNode(DockNode* root, std::uint32_t nodeId) noexcept;
    [[nodiscard]] static const DockNode* FindNode(const DockNode* root, std::uint32_t nodeId) noexcept;
    [[nodiscard]] static DockNode* FindLeaf(DockNode* root, std::uint32_t leafId) noexcept;
    [[nodiscard]] static const DockNode* FindLeaf(const DockNode* root, std::uint32_t leafId) noexcept;
    [[nodiscard]] static DockNode* FindLeafContaining(DockNode* root, std::uint32_t panelId) noexcept;
    [[nodiscard]] static const DockNode* FindLeafContaining(const DockNode* root, std::uint32_t panelId) noexcept;
    [[nodiscard]] static std::unique_ptr<DockNode>* FindNodeSlot(std::unique_ptr<DockNode>& root, std::uint32_t nodeId) noexcept;

private:
    [[nodiscard]] static std::unique_ptr<DockNode>* FindNodeSlotRecursive(std::unique_ptr<DockNode>& node, std::uint32_t nodeId) noexcept;
};

} // namespace kb::editor
