#include "EditorDockModel.hpp"

namespace kb::editor {

EditorDockModel::EditorDockModel()
    : panels_{
          DockPanel{ .title = "Hierarchy", .area = DockArea::Left },
          DockPanel{ .title = "Scene", .area = DockArea::Center, .detachable = false },
          DockPanel{ .title = "Inspector", .area = DockArea::Right },
          DockPanel{ .title = "Assets", .area = DockArea::Bottom },
          DockPanel{ .title = "Console", .area = DockArea::Bottom },
      } {
}

const std::vector<DockPanel>& EditorDockModel::Panels() const noexcept {
    return panels_;
}

std::vector<DockPanel> EditorDockModel::PanelsInArea(DockArea area) const {
    std::vector<DockPanel> result;
    for (const auto& panel : panels_) {
        if (panel.visible && panel.area == area) {
            result.push_back(panel);
        }
    }
    return result;
}

} // namespace kb::editor
