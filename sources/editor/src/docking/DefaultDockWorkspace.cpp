#include "docking/DefaultDockWorkspace.hpp"

#include "docking/DockNodeFactory.hpp"
#include "localization/EditorLocalization.hpp"

#include <utility>

namespace kb::editor {

std::string DefaultDockWorkspace::PanelTitle(DockPanelKind kind) {
    switch (kind) {
    case DockPanelKind::Hierarchy: return std::string{ EditorText("panel.hierarchy", "Hierarchy") };
    case DockPanelKind::Scene: return std::string{ EditorText("panel.scene", "Scene View") };
    case DockPanelKind::Inspector: return std::string{ EditorText("panel.inspector", "Inspector") };
    case DockPanelKind::Assets: return std::string{ EditorText("panel.assets", "Project Files") };
    case DockPanelKind::Console: return std::string{ EditorText("panel.console", "Console") };
    case DockPanelKind::ProjectSettings: return std::string{ EditorText("panel.projectSettings", "Project Settings") };
    case DockPanelKind::ScriptEditor: return std::string{ EditorText("panel.scriptEditor", "Script Editor") };
    case DockPanelKind::Plugins: return std::string{ EditorText("panel.plugins", "Plugins") };
    case DockPanelKind::MaterialEditor: return std::string{ EditorText("panel.materialEditor", "Material Editor") };
    case DockPanelKind::SkeletalMeshEditor: return std::string{ EditorText("panel.skeletalMeshEditor", "Skeletal Mesh Editor") };
    case DockPanelKind::AnimationClipEditor: return std::string{ EditorText("panel.animationClipEditor", "Animation Clip Editor") };
    case DockPanelKind::AnimatorEditor: return std::string{ EditorText("panel.animatorEditor", "Animator Editor") };
    case DockPanelKind::ParticleEditor: return std::string{ EditorText("panel.particleEditor", "21kb Particle System") };
    case DockPanelKind::EditorSettings: return std::string{ EditorText("panel.editorSettings", "Editor Settings") };
    case DockPanelKind::BuildGame: return std::string{ EditorText("panel.buildGame", "Build Game") };
    case DockPanelKind::TilemapEditor: return std::string{ EditorText("panel.tilemap", "2D Map") };
    case DockPanelKind::TimelineEditor: return "Timeline";
    case DockPanelKind::Generic: break;
    }
    return {};
}

// Each panel starts with its own title; Editor Settings gives them the new one when the language changes.
std::vector<DockPanel> DefaultDockWorkspace::CreatePanels() const {
    return {
        DockPanel{ .id = 20, .kind = DockPanelKind::TimelineEditor, .title = PanelTitle(DockPanelKind::TimelineEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 152, 144, 1100, 720 } },
        DockPanel{ .id = 17, .kind = DockPanelKind::TilemapEditor, .title = PanelTitle(DockPanelKind::TilemapEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 152, 144, 1180, 720 } },
        DockPanel{ .id = 1, .kind = DockPanelKind::Hierarchy, .title = PanelTitle(DockPanelKind::Hierarchy), .area = DockArea::Left, .floatingRect = DockRect{ 96, 96, 360, 520 } },
        DockPanel{ .id = 2, .kind = DockPanelKind::Scene, .title = PanelTitle(DockPanelKind::Scene), .area = DockArea::Center, .floatingRect = DockRect{ 128, 128, 720, 520 } },
        DockPanel{ .id = 4, .kind = DockPanelKind::Inspector, .title = PanelTitle(DockPanelKind::Inspector), .area = DockArea::Right, .floatingRect = DockRect{ 112, 112, 380, 560 } },
        DockPanel{ .id = 5, .kind = DockPanelKind::Assets, .title = PanelTitle(DockPanelKind::Assets), .area = DockArea::Bottom, .floatingRect = DockRect{ 128, 128, 620, 360 } },
        DockPanel{ .id = 6, .kind = DockPanelKind::Console, .title = PanelTitle(DockPanelKind::Console), .area = DockArea::Bottom, .floatingRect = DockRect{ 144, 144, 680, 340 } },
        DockPanel{ .id = 7, .kind = DockPanelKind::ProjectSettings, .title = PanelTitle(DockPanelKind::ProjectSettings), .area = DockArea::Right, .visible = false, .floatingRect = DockRect{ 160, 120, 420, 520 } },
        DockPanel{ .id = 8, .kind = DockPanelKind::ScriptEditor, .title = PanelTitle(DockPanelKind::ScriptEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 180, 140, 760, 560 } },
        DockPanel{ .id = 9, .kind = DockPanelKind::Plugins, .title = PanelTitle(DockPanelKind::Plugins), .area = DockArea::Right, .visible = false, .floatingRect = DockRect{ 176, 132, 560, 520 } },
        DockPanel{ .id = 10, .kind = DockPanelKind::MaterialEditor, .title = PanelTitle(DockPanelKind::MaterialEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 132, 124, 760, 560 } },
        DockPanel{ .id = 11, .kind = DockPanelKind::SkeletalMeshEditor, .title = PanelTitle(DockPanelKind::SkeletalMeshEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 136, 128, 860, 620 } },
        DockPanel{ .id = 12, .kind = DockPanelKind::AnimationClipEditor, .title = PanelTitle(DockPanelKind::AnimationClipEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 140, 132, 860, 620 } },
        DockPanel{ .id = 13, .kind = DockPanelKind::AnimatorEditor, .title = PanelTitle(DockPanelKind::AnimatorEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 144, 136, 860, 620 } },
        DockPanel{ .id = 14, .kind = DockPanelKind::ParticleEditor, .title = PanelTitle(DockPanelKind::ParticleEditor), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 148, 140, 900, 640 } },
        DockPanel{ .id = 15, .kind = DockPanelKind::EditorSettings, .title = PanelTitle(DockPanelKind::EditorSettings), .area = DockArea::Right, .visible = false, .floatingRect = DockRect{ 168, 128, 720, 600 } },
        DockPanel{ .id = 16, .kind = DockPanelKind::BuildGame, .title = PanelTitle(DockPanelKind::BuildGame), .area = DockArea::Center, .visible = false, .floatingRect = DockRect{ 152, 144, 1180, 720 } },
    };
}

std::unique_ptr<DockNode> DefaultDockWorkspace::CreateRoot(std::uint32_t& nextNodeId) const {
    auto left = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 1 });
    // Only the everyday panels start open. The specialist editors, settings, plugins and Build Game stay closed
    // and dock into their area (center / right) when something opens them: a double click on an asset, or a menu row.
    auto center = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 2 });
    auto right = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 4 });
    auto bottom = DockNodeFactory::MakeLeaf(Next(nextNodeId), { 5, 6 });
    auto middle = DockNodeFactory::MakeSplit(Next(nextNodeId), DockSplitAxis::Horizontal, 0.72F, std::move(center), std::move(right));
    auto top = DockNodeFactory::MakeSplit(Next(nextNodeId), DockSplitAxis::Horizontal, 0.18F, std::move(left), std::move(middle));
    return DockNodeFactory::MakeSplit(Next(nextNodeId), DockSplitAxis::Vertical, 0.73F, std::move(top), std::move(bottom));
}

std::uint32_t DefaultDockWorkspace::Next(std::uint32_t& nextNodeId) noexcept {
    return nextNodeId++;
}

} // namespace kb::editor
