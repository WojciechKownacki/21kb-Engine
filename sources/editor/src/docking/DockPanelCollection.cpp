#include "docking/DockPanelCollection.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {

DockPanelCollection::DockPanelCollection(std::vector<DockPanel> panels) noexcept
    : panels_(std::move(panels)) {}

const std::vector<DockPanel>& DockPanelCollection::All() const noexcept {
    return panels_;
}

std::vector<DockPanel> DockPanelCollection::InArea(DockArea area) const {
    std::vector<DockPanel> result;
    for (const DockPanel& panel : panels_) {
        if (panel.visible && panel.area == area) {
            result.push_back(panel);
        }
    }
    return result;
}

const DockPanel* DockPanelCollection::Find(std::uint32_t panelId) const noexcept {
    for (const DockPanel& panel : panels_) {
        if (panel.id == panelId) {
            return &panel;
        }
    }
    return nullptr;
}

DockPanel* DockPanelCollection::Find(std::uint32_t panelId) noexcept {
    for (DockPanel& panel : panels_) {
        if (panel.id == panelId) {
            return &panel;
        }
    }
    return nullptr;
}

void DockPanelCollection::MoveFloatingPanel(std::uint32_t panelId, int x, int y) noexcept {
    if (DockPanel* panel = Find(panelId); panel != nullptr) {
        panel->floatingRect.x = x;
        panel->floatingRect.y = y;
    }
}

void DockPanelCollection::ResizeFloatingPanel(std::uint32_t panelId, int width, int height) noexcept {
    if (DockPanel* panel = Find(panelId); panel != nullptr) {
        panel->floatingRect.width = std::max(260, width);
        panel->floatingRect.height = std::max(180, height);
    }
}

} // namespace kb::editor
