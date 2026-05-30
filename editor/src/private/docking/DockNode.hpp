#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace kb::editor {

struct DockNode {
    enum class Kind : std::uint8_t {
        Leaf,
        Split,
    };

    Kind kind = Kind::Leaf;
    std::uint32_t id = 0;
    DockSplitAxis axis = DockSplitAxis::Horizontal;
    float ratio = 0.5F;
    std::unique_ptr<DockNode> first{};
    std::unique_ptr<DockNode> second{};
    std::vector<std::uint32_t> panels{};
    std::uint32_t activePanelId = 0;
};

} // namespace kb::editor
