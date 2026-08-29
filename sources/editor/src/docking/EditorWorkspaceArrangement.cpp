#include "docking/EditorWorkspaceArrangement.hpp"

#include "docking/EditorDockModel.hpp"

namespace kb::editor {
namespace {

// The particle editor's own host owns this panel's session: it reopens the document
// and creates the floating window that goes with it.
constexpr std::uint32_t kParticleEditorPanelId = 14U;

} // namespace

EditorLayoutPreset EditorWorkspaceArrangement::Capture(EditorDockModel& dockModel) {
    EditorLayoutPreset preset;
    preset.tree = dockModel.Commands().SerializeWorkspace();
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        preset.panels.push_back({
            .panelId = panel.id,
            .visible = panel.visible,
            .area = panel.area,
            .floatingRect = panel.floatingRect,
        });
    }
    return preset;
}

bool EditorWorkspaceArrangement::Apply(EditorDockModel& dockModel, const EditorLayoutPreset& preset) {
    const bool treeAccepted =
        preset.tree.empty() || dockModel.Commands().RestoreWorkspace(preset.tree);
    for (const EditorPanelSession& session : preset.panels) {
        if (session.panelId == kParticleEditorPanelId) {
            continue;
        }
        const DockPanel* panel = dockModel.Queries().FindPanel(session.panelId);
        if (panel == nullptr) {
            continue;
        }
        if (!session.visible) {
            if (panel->visible) {
                static_cast<void>(dockModel.Commands().ClosePanel(session.panelId));
            }
            continue;
        }
        if (session.area == DockArea::Floating && panel->area != DockArea::Floating) {
            dockModel.Commands().UndockPanel(session.panelId, session.floatingRect);
        }
    }
    return treeAccepted;
}

} // namespace kb::editor
