#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockNodeIdSource;

class DockCenterPanelInserter {
public:
    DockCenterPanelInserter() = delete;

    static void Dock(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, DockNodeIdSource& nodeIds);
};

} // namespace kb::editor
