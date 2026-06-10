#include "app/project_settings/EditorProjectSettingsPointerController.hpp"

#if defined(_WIN32)
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {

EditorProjectSettingsPointerController::EditorProjectSettingsPointerController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorProjectSettingsPointerController::HandlePointerDown(const RECT& content, int x, int y) {
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::HitTest(content, x, y);
    switch (hit.kind) {
    case ProjectSettingsHitKind::MappingContextField:
        return sceneContext_.CycleProjectInputMappingContext();
    case ProjectSettingsHitKind::EnabledCheckbox:
        return sceneContext_.ToggleProjectInputEnabled();
    case ProjectSettingsHitKind::None:
    default:
        return false;
    }
}

} // namespace kb::editor

#endif
