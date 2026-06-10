#include "app/project_settings/EditorProjectSettingsPointerController.hpp"

#if defined(_WIN32)
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>
#include <vector>

namespace kb::editor {

EditorProjectSettingsPointerController::EditorProjectSettingsPointerController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorProjectSettingsPointerController::HandlePointerDown(const RECT& content, int x, int y) {
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::HitTest(content, sceneContext_, x, y);
    switch (hit.kind) {
    case ProjectSettingsHitKind::CategoryItem:
        // SelectCategory also closes any open dropdown; always repaint to show the
        // pressed row even when the same category is re-clicked.
        static_cast<void>(sceneContext_.ProjectSettings().SelectCategory(hit.index));
        return true;
    case ProjectSettingsHitKind::MappingContextField:
        sceneContext_.ProjectSettings().ToggleMappingContextDropdown();
        return true;
    case ProjectSettingsHitKind::MappingContextOption: {
        const std::vector<std::string> options = sceneContext_.ProjectInputMappingContextOptions();
        const bool changed = hit.index >= 0 && static_cast<std::size_t>(hit.index) < options.size() &&
                             sceneContext_.SetProjectInputMappingContext(options[static_cast<std::size_t>(hit.index)]);
        static_cast<void>(sceneContext_.CloseProjectSettingsDropdowns());
        // Always repaint to dismiss the list, even when the selection was unchanged.
        static_cast<void>(changed);
        return true;
    }
    case ProjectSettingsHitKind::EnabledCheckbox: {
        const bool closed = sceneContext_.CloseProjectSettingsDropdowns();
        return sceneContext_.ToggleProjectInputEnabled() || closed;
    }
    case ProjectSettingsHitKind::None:
    default:
        // A click anywhere else in the panel dismisses an open dropdown.
        return sceneContext_.CloseProjectSettingsDropdowns();
    }
}

} // namespace kb::editor

#endif
