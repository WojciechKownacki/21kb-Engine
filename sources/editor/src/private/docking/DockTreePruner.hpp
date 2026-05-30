#pragma once

#include "docking/DockNode.hpp"

#include <memory>

namespace kb::editor {

class DockTreePruner {
public:
    DockTreePruner() = delete;

    static void PruneEmptyBranches(std::unique_ptr<DockNode>& root) noexcept;

private:
    [[nodiscard]] static bool PruneRecursive(std::unique_ptr<DockNode>& node) noexcept;
};

} // namespace kb::editor
