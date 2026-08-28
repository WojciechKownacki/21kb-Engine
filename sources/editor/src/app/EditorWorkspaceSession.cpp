#include "app/EditorWorkspaceSession.hpp"

#include "docking/EditorDockModel.hpp"
#include "scene/EditorSceneContext.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <utility>

namespace kb::editor {
namespace {

// The particle editor's own host owns this panel's session: it reopens the document
// and creates the floating window that goes with it.
constexpr std::uint32_t kParticleEditorPanelId = 14U;

} // namespace

void EditorWorkspaceSession::Restore(EditorDockModel& dockModel, EditorSceneContext& context) {
    const EditorConfiguration& configuration = context.EditorConfig();
    if (!configuration.layout.empty() && !dockModel.Commands().RestoreWorkspace(configuration.layout)) {
        context.Console().Warning("Layout",
            "The saved workspace layout does not fit this build and was discarded.");
    }
    for (const EditorPanelSession& session : configuration.panels) {
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
}

void EditorWorkspaceSession::Save(EditorDockModel& dockModel, EditorSceneContext& context) {
    EditorConfiguration configuration = context.EditorConfig();
    configuration.layout = dockModel.Commands().SerializeWorkspace();
    configuration.panels.clear();
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        EditorPanelSession session{
            .panelId = panel.id,
            .visible = panel.visible,
            .area = panel.area,
            .floatingRect = panel.floatingRect,
        };
        if (panel.id == kParticleEditorPanelId && context.ParticleEditorSessionPath().has_value()) {
            session.documentPath = *context.ParticleEditorSessionPath();
        }
        configuration.panels.push_back(std::move(session));
    }
    static_cast<void>(context.SaveEditorConfig(std::move(configuration)));
}

} // namespace kb::editor
