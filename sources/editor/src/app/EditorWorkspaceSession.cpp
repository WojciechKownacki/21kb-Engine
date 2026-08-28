#include "app/EditorWorkspaceSession.hpp"

#include "docking/EditorWorkspaceArrangement.hpp"
#include "scene/EditorSceneContext.hpp"
#include "settings/EditorConfigurationStore.hpp"

#include <utility>

namespace kb::editor {
namespace {

constexpr std::uint32_t kParticleEditorPanelId = 14U;

} // namespace

void EditorWorkspaceSession::Restore(EditorDockModel& dockModel, EditorSceneContext& context) {
    const EditorConfiguration& configuration = context.EditorConfig();
    const EditorLayoutPreset preset{
        .tree = configuration.layout,
        .panels = configuration.panels,
    };
    if (!EditorWorkspaceArrangement::Apply(dockModel, preset)) {
        context.Console().Warning("Layout",
            "The saved workspace layout does not fit this build and was discarded.");
    }
}

void EditorWorkspaceSession::Save(EditorDockModel& dockModel, EditorSceneContext& context) {
    SaveAs(dockModel, context, context.EditorConfig().layoutName);
}

void EditorWorkspaceSession::SaveAs(
    EditorDockModel& dockModel, EditorSceneContext& context, std::string layoutName) {
    EditorLayoutPreset preset = EditorWorkspaceArrangement::Capture(dockModel);
    EditorConfiguration configuration = context.EditorConfig();
    configuration.layoutName = std::move(layoutName);
    configuration.layout = std::move(preset.tree);
    configuration.panels = std::move(preset.panels);
    for (EditorPanelSession& session : configuration.panels) {
        if (session.panelId == kParticleEditorPanelId &&
            context.ParticleEditorSessionPath().has_value()) {
            session.documentPath = *context.ParticleEditorSessionPath();
        }
    }
    static_cast<void>(context.SaveEditorConfig(std::move(configuration)));
}

} // namespace kb::editor
