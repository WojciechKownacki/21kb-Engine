#include "docking/DefaultDockWorkspace.hpp"

#include "docking/DockNodeFactory.hpp"

#include <utility>

namespace kb::editor {

std::vector<DockPanel> DefaultDockWorkspace::CreatePanels() const {
    return {
        DockPanel{ .id = 1, .kind = DockPanelKind::Hierarchy, .title = "Hierarchy", .area = DockArea::Left, .floatingRect = DockRect{ 96, 96, 360, 520 } },
        DockPanel{ .id = 2, .kind = DockPanelKind::Scene, .title = "Scene View", .area = DockArea::Center, .floatingRect = DockRect{ 128, 128, 720, 520 } },
        DockPanel{ .id = 4, .kind = DockPanelKind::Inspector, .title = "Inspector", .area = DockArea::Right, .floatingRect = DockRect{ 112, 112, 380, 560 } },
        DockPanel{ .id = 5, .kind = DockPanelKind::Assets, .title = "Project Files", .area = DockArea::Bottom, .floatingRect = DockRect{ 128, 128, 620, 360 } },
        DockPanel{ .id = 6, .kind = DockPanelKind::Console, .title = "Console", .area = DockArea::Bottom, .floatingRect = DockRect{ 144, 144, 680, 340 } },
        DockPanel{ .id = 7, .kind = DockPanelKind::ProjectSettings, .title = "Project Settings", .area = DockArea::Right, .floatingRect = DockRect{ 160, 120, 420, 520 } },
        DockPanel{ .id = 8, .kind = DockPanelKind::ScriptEditor, .title = "Script Editor", .area = DockArea::Center, .floatingRect = DockRect{ 180, 140, 760, 560 } },
    };
}

std::unique_ptr<DockNode> DefaultDockWorkspace::CreateRoot(std::uint32_t& nextNodeId) const {
    auto left = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 1 });
    auto center = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 2, 8 });
    auto right = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 4, 7 });
    auto bottom = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 5, 6 });
    auto middle = DockNodeFactory::MakeSplit(Next(nextNodeId), DockSplitAxis::Horizontal, 0.72F, std::move(center), std::move(right));
    auto top = DockNodeFactory::MakeSplit(Next(nextNodeId), DockSplitAxis::Horizontal, 0.18F, std::move(left), std::move(middle));
    return DockNodeFactory::MakeSplit(Next(nextNodeId), DockSplitAxis::Vertical, 0.73F, std::move(top), std::move(bottom));
}

std::uint32_t DefaultDockWorkspace::Next(std::uint32_t& nextNodeId) noexcept {
    return nextNodeId++;
}

} // namespace kb::editor
