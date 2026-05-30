#pragma once

#include "docking/DockNode.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockPanelRemover {
public:
    DockPanelRemover() = delete;

    static void RemovePanel(std::unique_ptr<DockNode>& root, std::uint32_t panelId) noexcept;
};

} // namespace kb::editor
