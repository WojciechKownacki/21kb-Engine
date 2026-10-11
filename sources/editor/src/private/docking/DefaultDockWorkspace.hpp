#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kb::editor {

class DefaultDockWorkspace {
public:
    [[nodiscard]] std::vector<DockPanel> CreatePanels() const;
    // A panel's own title in the current editor language ("" for a kind without one). A panel
    // showing an open file carries that file's name instead.
    [[nodiscard]] static std::string PanelTitle(DockPanelKind kind);
    [[nodiscard]] std::unique_ptr<DockNode> CreateRoot(std::uint32_t& nextNodeId) const;

private:
    [[nodiscard]] static std::uint32_t Next(std::uint32_t& nextNodeId) noexcept;
};

} // namespace kb::editor
