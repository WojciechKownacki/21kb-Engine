#include "app/project_settings/EditorProjectSettingsPointerController.hpp"

#if defined(_WIN32)
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>
#include <vector>

namespace kb::editor {

EditorProjectSettingsPointerController::EditorProjectSettingsPointerController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

namespace {

[[nodiscard]] EditorRenderBackend BackendForOption(int index) noexcept {
    switch (index) {
    case 1:
        return EditorRenderBackend::DirectX12;
    case 2:
        return EditorRenderBackend::Vulkan;
    case 0:
    default:
        return EditorRenderBackend::Auto;
    }
}

[[nodiscard]] bool HandleProjectSettingsPointerDown(
    EditorSceneContext& sceneContext,
    EditorRenderBackendSettings* renderBackendSettings,
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
    case ProjectSettingsHitKind::EnabledCheckbox: {
        const bool closed = sceneContext.CloseProjectSettingsDropdowns();
        return sceneContext.ToggleProjectInputEnabled() || closed;
    }
    case ProjectSettingsHitKind::RenderBackendOption: {
        if (renderBackendSettings == nullptr) {
            return sceneContext.CloseProjectSettingsDropdowns();
        }
        const EditorRenderBackend previousBackend = renderBackendSettings->Backend();
        renderBackendSettings->SetBackend(BackendForOption(hit.index));
        if (renderBackendSettings->Backend() != previousBackend) {
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
    return HandleProjectSettingsPointerDown(sceneContext_, nullptr, content, x, y);
}

bool EditorProjectSettingsPointerController::HandlePointerDown(const RECT& content, int x, int y, EditorRenderBackendSettings& renderBackendSettings) {
    return HandleProjectSettingsPointerDown(sceneContext_, &renderBackendSettings, content, x, y);
}

bool EditorProjectSettingsPointerController::UpdateHover(const RECT& content, int x, int y) {
    if (!sceneContext_.ProjectSettings().IsMappingContextDropdownOpen()) {
        return sceneContext_.ProjectSettings().SetHoveredOption(-1);
    }
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::HitTest(content, sceneContext_, x, y);
    const int hovered = hit.kind == ProjectSettingsHitKind::MappingContextOption ? hit.index : -1;
    return sceneContext_.ProjectSettings().SetHoveredOption(hovered);
}

bool EditorProjectSettingsPointerController::UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y) {
    if (content.has_value() && x >= content->left && x < content->right && y >= content->top && y < content->bottom) {
        return UpdateHover(*content, x, y);
    }
    return sceneContext_.ProjectSettings().SetHoveredOption(-1);
}

bool EditorProjectSettingsPointerController::Contains(const std::optional<RECT>& content, int x, int y) const noexcept {
    return content.has_value() && x >= content->left && x < content->right && y >= content->top && y < content->bottom;
}

} // namespace kb::editor

#endif
