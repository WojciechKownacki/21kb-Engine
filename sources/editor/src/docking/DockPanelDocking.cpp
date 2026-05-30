#include "docking/DockPanelDocking.hpp"

namespace kb::editor {

void DockPanelDocking::Undock(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t panelId, DockRect floatingRect) noexcept {
    DockPanel* panel = panels.Find(panelId);
    if (panel == nullptr || !panel->detachable) {
        return;
    }

    DockTreeMutation::RemovePanel(root, panelId);
    DockTreeMutation::PruneEmptyBranches(root);
    panel->area = DockArea::Floating;
    panel->floatingRect = floatingRect;
}

void DockPanelDocking::Dock(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t panelId, const DockDropPreview& target, DockTreeMutation::NextNodeIdFn nextNodeId, void* context) {
    DockPanel* panel = panels.Find(panelId);
    if (panel == nullptr) {
        return;
    }

    DockTreeMutation::RemovePanel(root, panelId);
    DockTreeMutation::PruneEmptyBranches(root);
    panel->area = AreaForZone(target.zone);
    DockTreeMutation::DockPanel(root, panelId, target, nextNodeId, context);
}

DockArea DockPanelDocking::AreaForZone(DockDropZone zone) noexcept {
    switch (zone) {
    case DockDropZone::Left:
        return DockArea::Left;
    case DockDropZone::Right:
        return DockArea::Right;
    case DockDropZone::Bottom:
        return DockArea::Bottom;
    case DockDropZone::Top:
    case DockDropZone::Center:
    case DockDropZone::None:
    default:
        return DockArea::Center;
    }
}

} // namespace kb::editor
