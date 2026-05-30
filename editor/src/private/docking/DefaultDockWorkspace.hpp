#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace kb::editor {

class DefaultDockWorkspace {
public:
    [[nodiscard]] std::vector<DockPanel> CreatePanels() const;
    [[nodiscard]] std::unique_ptr<DockNode> CreateRoot(std::uint32_t& nextNodeId) const;

private:
    [[nodiscard]] static std::uint32_t Next(std::uint32_t& nextNodeId) noexcept;
};

} // namespace kb::editor
