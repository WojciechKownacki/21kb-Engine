#include "app/project_settings/EditorProjectSettingsPointerController.hpp"

#if defined(_WIN32)
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <vector>

namespace kb::editor {

EditorProjectSettingsPointerController::EditorProjectSettingsPointerController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

namespace {

[[nodiscard]] kb::project::ProjectSceneLightingPath LightingPathForOption(int index) noexcept {
    switch (index) {
    case 1:
        return kb::project::ProjectSceneLightingPath::ForwardPlus;
    case 2:
        return kb::project::ProjectSceneLightingPath::Deferred;
    case 0:
    default:
        return kb::project::ProjectSceneLightingPath::Forward;
    }
}

[[nodiscard]] ProjectSettingsTooltipKind TooltipKindForHit(ProjectSettingsPanelRenderer::Hit hit) noexcept {
    switch (hit.kind) {
    case ProjectSettingsHitKind::MappingContextField:
        return ProjectSettingsTooltipKind::MappingContext;
    case ProjectSettingsHitKind::PhysicsLayersField:
        return ProjectSettingsTooltipKind::PhysicsLayers;
    case ProjectSettingsHitKind::EnabledCheckbox:
        return ProjectSettingsTooltipKind::InputEnabled;
    case ProjectSettingsHitKind::LightingPathOption:
        return ProjectSettingsTooltipKind::LightingPath;
    case ProjectSettingsHitKind::None:
    case ProjectSettingsHitKind::CategoryItem:
    case ProjectSettingsHitKind::MappingContextOption:
    case ProjectSettingsHitKind::PhysicsLayersOption:
    default:
        return ProjectSettingsTooltipKind::None;
    }
}

[[nodiscard]] bool HandleProjectSettingsPointerDown(
    EditorSceneContext& sceneContext,
    const RECT& content,
    int x,
    int y) {
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::HitTest(content, sceneContext, x, y);
    switch (hit.kind) {
    case ProjectSettingsHitKind::CategoryItem:
        // SelectCategory also closes any open dropdown; always repaint to show the
        // pressed row even when the same category is re-clicked.
        static_cast<void>(sceneContext.ProjectSettings().SelectCategory(hit.index));
        return true;
    case ProjectSettingsHitKind::MappingContextField:
        sceneContext.ProjectSettings().ToggleMappingContextDropdown();
        return true;
    case ProjectSettingsHitKind::MappingContextOption: {
        const std::vector<std::string> options = sceneContext.ProjectInputMappingContextOptions();
        const bool changed = hit.index >= 0 && static_cast<std::size_t>(hit.index) < options.size() &&
                             sceneContext.SetProjectInputMappingContext(options[static_cast<std::size_t>(hit.index)]);
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        // Always repaint to dismiss the list, even when the selection was unchanged.
        static_cast<void>(changed);
        return true;
    }
    case ProjectSettingsHitKind::PhysicsLayersField:
        sceneContext.ProjectSettings().TogglePhysicsLayersDropdown();
        return true;
    case ProjectSettingsHitKind::PhysicsLayersOption: {
        const std::vector<std::string> options = sceneContext.ProjectPhysicsLayersAssetOptions();
        const bool changed = hit.index >= 0 && static_cast<std::size_t>(hit.index) < options.size() &&
                             sceneContext.SetProjectPhysicsLayersAsset(options[static_cast<std::size_t>(hit.index)]);
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        static_cast<void>(changed);
        return true;
    }
    case ProjectSettingsHitKind::EnabledCheckbox: {
        const bool closed = sceneContext.CloseProjectSettingsDropdowns();
        return sceneContext.ToggleProjectInputEnabled() || closed;
    }
    case ProjectSettingsHitKind::LightingPathOption: {
        const bool changed = sceneContext.SetProjectSceneLightingPath(LightingPathForOption(hit.index));
        if (changed) {
            sceneContext.MarkSceneRenderDirty();
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    }
    case ProjectSettingsHitKind::None:
    default:
        // A click anywhere else in the panel dismisses an open dropdown.
        return sceneContext.CloseProjectSettingsDropdowns();
    }
}

} // namespace

bool EditorProjectSettingsPointerController::HandlePointerDown(const RECT& content, int x, int y) {
    return HandleProjectSettingsPointerDown(sceneContext_, content, x, y);
}

bool EditorProjectSettingsPointerController::UpdateHover(const RECT& content, int x, int y) {
    bool changed = false;
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::TooltipHitTest(content, sceneContext_, x, y);
    changed = sceneContext_.ProjectSettings().SetTooltip(TooltipKindForHit(hit), x, y) || changed;
    if (!sceneContext_.ProjectSettings().IsMappingContextDropdownOpen() && !sceneContext_.ProjectSettings().IsPhysicsLayersDropdownOpen()) {
        return sceneContext_.ProjectSettings().SetHoveredOption(-1) || changed;
    }
    const int hovered = (hit.kind == ProjectSettingsHitKind::MappingContextOption || hit.kind == ProjectSettingsHitKind::PhysicsLayersOption) ? hit.index : -1;
    return sceneContext_.ProjectSettings().SetHoveredOption(hovered) || changed;
}

bool EditorProjectSettingsPointerController::UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y) {
    if (content.has_value() && x >= content->left && x < content->right && y >= content->top && y < content->bottom) {
        return UpdateHover(*content, x, y);
    }
    const bool clearedOption = sceneContext_.ProjectSettings().SetHoveredOption(-1);
    return sceneContext_.ProjectSettings().ClearTooltip() || clearedOption;
}

bool EditorProjectSettingsPointerController::Contains(const std::optional<RECT>& content, int x, int y) const noexcept {
    return content.has_value() && x >= content->left && x < content->right && y >= content->top && y < content->bottom;
}

} // namespace kb::editor

#endif
