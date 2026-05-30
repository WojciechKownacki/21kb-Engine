#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockNodeIdSource;

class DockSplitPanelInserter {
public:
    DockSplitPanelInserter() = delete;

    static void Dock(std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, DockNodeIdSource& nodeIds);

private:
    static void AttachToRoot(std::unique_ptr<DockNode>& root, std::unique_ptr<DockNode> dropped, DockSplitAxis axis, float ratio, bool droppedFirst, DockNodeIdSource& nodeIds);
};

} // namespace kb::editor
