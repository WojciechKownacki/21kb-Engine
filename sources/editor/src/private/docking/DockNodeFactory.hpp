#pragma once

#include "docking/DockNode.hpp"

#include <memory>
#include <vector>

namespace kb::editor {

class DockNodeFactory {
public:
    DockNodeFactory() = delete;

    [[nodiscard]] static std::unique_ptr<DockNode> MakeLeaf(std::uint32_t id, std::vector<std::uint32_t> panels);
    [[nodiscard]] static std::unique_ptr<DockNode> MakeSplit(std::uint32_t id, DockSplitAxis axis, float ratio, std::unique_ptr<DockNode> first, std::unique_ptr<DockNode> second);
};

} // namespace kb::editor
