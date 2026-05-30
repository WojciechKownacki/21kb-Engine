#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>

namespace kb::editor {

class DockLeafPanelOrder {
public:
    DockLeafPanelOrder() = delete;

    static void Activate(DockNode* root, std::uint32_t panelId) noexcept;
    static void Reorder(DockNode* root, std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex);
    [[nodiscard]] static std::uint32_t Count(const DockNode* root, std::uint32_t leafId) noexcept;
};

} // namespace kb::editor
