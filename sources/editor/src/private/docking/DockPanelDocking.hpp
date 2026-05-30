#pragma once

#include "docking/DockNode.hpp"
#include "docking/DockPanelCollection.hpp"
#include "docking/DockTreeMutation.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class DockPanelDocking {
public:
    DockPanelDocking() = delete;

    static void Undock(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t panelId, DockRect floatingRect) noexcept;
    static void Dock(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, DockTreeMutation::NextNodeIdFn nextNodeId, void* context);

private:
    [[nodiscard]] static DockArea AreaForZone(DockDropZone zone) noexcept;
};

} // namespace kb::editor
