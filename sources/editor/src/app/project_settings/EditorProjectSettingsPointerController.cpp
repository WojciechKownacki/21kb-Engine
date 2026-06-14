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

void ToggleGraphicsOption(EditorRenderBackendSettings& settings, int index) noexcept {
    switch (index) {
    case 0:
        settings.TogglePostProcessEnabled();
        return;
    case 1:
        settings.ToggleBloomEnabled();
        return;
    case 2:
        settings.ToggleShadowsEnabled();
        return;
    case 3:
        settings.ToggleSelectionOutlineEnabled();
        return;
    case 4:
        settings.ToggleGpuDrivenEnabled();
        return;
    default:
        return;
    }
}

[[nodiscard]] EditorAntiAliasingMode AntiAliasingModeForOption(int index) noexcept {
    switch (index) {
    case 1:
        return EditorAntiAliasingMode::Fxaa;
    case 2:
        return EditorAntiAliasingMode::Taa;
    case 3:
        return EditorAntiAliasingMode::Msaa;
    case 0:
    default:
        return EditorAntiAliasingMode::None;
    }
}

[[nodiscard]] std::uint8_t MsaaSamplesForOption(int index) noexcept {
    switch (index) {
    case 1:
        return 2U;
    case 2:
        return 4U;
    case 0:
    default:
        return 0U;
    }
}

[[nodiscard]] ProjectSettingsTooltipKind GraphicsToggleTooltipKind(int index) noexcept {
    switch (index) {
    case 0:
        return ProjectSettingsTooltipKind::PostProcess;
    case 1:
        return ProjectSettingsTooltipKind::Bloom;
    case 2:
        return ProjectSettingsTooltipKind::Shadows;
    case 3:
        return ProjectSettingsTooltipKind::SelectionOutline;
    case 4:
        return ProjectSettingsTooltipKind::GpuDriven;
    default:
        return ProjectSettingsTooltipKind::None;
    }
}

[[nodiscard]] ProjectSettingsTooltipKind TooltipKindForHit(ProjectSettingsPanelRenderer::Hit hit) noexcept {
    switch (hit.kind) {
    case ProjectSettingsHitKind::MappingContextField:
        return ProjectSettingsTooltipKind::MappingContext;
    case ProjectSettingsHitKind::EnabledCheckbox:
        return ProjectSettingsTooltipKind::InputEnabled;
    case ProjectSettingsHitKind::RenderBackendOption:
        return ProjectSettingsTooltipKind::RenderBackend;
    case ProjectSettingsHitKind::AntiAliasingMode:
        return ProjectSettingsTooltipKind::AntiAliasing;
    case ProjectSettingsHitKind::MsaaOption:
        return ProjectSettingsTooltipKind::MsaaSamples;
    case ProjectSettingsHitKind::GraphicsToggle:
        return GraphicsToggleTooltipKind(hit.index);
    case ProjectSettingsHitKind::None:
    case ProjectSettingsHitKind::CategoryItem:
    case ProjectSettingsHitKind::MappingContextOption:
    default:
        return ProjectSettingsTooltipKind::None;
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
    case ProjectSettingsHitKind::GraphicsToggle: {
        if (renderBackendSettings == nullptr) {
            return sceneContext.CloseProjectSettingsDropdowns();
        }
        const std::uint64_t previousGeneration = renderBackendSettings->Generation();
        ToggleGraphicsOption(*renderBackendSettings, hit.index);
        if (renderBackendSettings->Generation() != previousGeneration) {
            sceneContext.MarkSceneRenderDirty();
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    }
    case ProjectSettingsHitKind::AntiAliasingMode: {
        if (renderBackendSettings == nullptr) {
            return sceneContext.CloseProjectSettingsDropdowns();
        }
        const std::uint64_t previousGeneration = renderBackendSettings->Generation();
        renderBackendSettings->SetAntiAliasingMode(AntiAliasingModeForOption(hit.index));
        if (renderBackendSettings->Generation() != previousGeneration) {
            sceneContext.MarkSceneRenderDirty();
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    }
    case ProjectSettingsHitKind::MsaaOption: {
        if (renderBackendSettings == nullptr) {
            return sceneContext.CloseProjectSettingsDropdowns();
        }
        if (renderBackendSettings->AntiAliasingMode() != EditorAntiAliasingMode::Msaa) {
            static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
            return false;
        }
        const std::uint64_t previousGeneration = renderBackendSettings->Generation();
        renderBackendSettings->SetMsaaSamples(MsaaSamplesForOption(hit.index));
        if (renderBackendSettings->Generation() != previousGeneration) {
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
    bool changed = false;
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::TooltipHitTest(content, sceneContext_, x, y);
    changed = sceneContext_.ProjectSettings().SetTooltip(TooltipKindForHit(hit), x, y) || changed;
    if (!sceneContext_.ProjectSettings().IsMappingContextDropdownOpen()) {
        return sceneContext_.ProjectSettings().SetHoveredOption(-1) || changed;
    }
    const int hovered = hit.kind == ProjectSettingsHitKind::MappingContextOption ? hit.index : -1;
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
