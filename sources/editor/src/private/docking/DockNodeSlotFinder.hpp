#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockNodeSlotFinder {
public:
    DockNodeSlotFinder() = delete;

    [[nodiscard]] static std::unique_ptr<DockNode>* FindNodeSlot(std::unique_ptr<DockNode>& root, std::uint32_t nodeId) noexcept;

private:
    [[nodiscard]] static std::unique_ptr<DockNode>* FindNodeSlotRecursive(std::unique_ptr<DockNode>& node, std::uint32_t nodeId) noexcept;
};

} // namespace kb::editor
