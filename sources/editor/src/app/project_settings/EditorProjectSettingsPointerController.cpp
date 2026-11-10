#include "app/project_settings/EditorProjectSettingsPointerController.hpp"

#if defined(_WIN32)
#include "app/EditorCrashBreadcrumbs.hpp"
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
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

[[nodiscard]] const char* AntiAliasingModeName(EditorAntiAliasingMode mode) noexcept {
    switch (mode) {
    case EditorAntiAliasingMode::None:
        return "None";
    case EditorAntiAliasingMode::Fxaa:
        return "FXAA";
    case EditorAntiAliasingMode::Taa:
        return "TAA";
    case EditorAntiAliasingMode::Msaa:
        return "MSAA";
    }
    return "Unknown";
}

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "1" : "0";
}

[[nodiscard]] std::uint8_t MsaaSamplesForOption(int index) noexcept {
    switch (index) {
    case 1:
        return 2U;
    case 2:
        return 4U;
    case 3:
        return 8U;
    case 4:
        return 16U;
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

[[nodiscard]] ProjectSettingsTooltipKind QualityTooltipKind(int row) noexcept {
    switch (row) {
    case 0:
        return ProjectSettingsTooltipKind::ShadowQuality;
    case 1:
        return ProjectSettingsTooltipKind::ShadowQualityMobile;
    case 2:
        return ProjectSettingsTooltipKind::GiQuality;
    case 3:
        return ProjectSettingsTooltipKind::GiQualityMobile;
    default:
        return ProjectSettingsTooltipKind::None;
    }
}

// The simulation settings after one click of a stepper's minus (direction 0) or plus (1): stepper 0 is the fixed
// step, 1 to 3 the gravity axes and 4 the time scale. Values stay inside their limits and keep two decimals.
[[nodiscard]] kb::project::ProjectSimulationSettings SteppedSimulation(
    kb::project::ProjectSimulationSettings simulation, int stepper, int direction) noexcept {
    const auto sign = direction == 0 ? -1.0F : 1.0F;
    const auto stepped = [sign](float value, float increment, float low, float high) noexcept {
        return std::clamp(std::round((value + sign * increment) * 100.0F) / 100.0F, low, high);
    };
    if (stepper == 0) {
        const std::int64_t hz = static_cast<std::int64_t>(simulation.fixedStepHz) +
            static_cast<std::int64_t>(sign) * kProjectSettingsFixedStepIncrement;
        simulation.fixedStepHz = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
            hz, kb::project::kProjectFixedStepHzMin, kb::project::kProjectFixedStepHzMax));
    } else if (stepper >= 1 && stepper <= 3) {
        float& axis = simulation.gravity[static_cast<std::size_t>(stepper - 1)];
        axis = stepped(axis, kProjectSettingsGravityIncrement, -kb::project::kProjectGravityLimit, kb::project::kProjectGravityLimit);
    } else if (stepper == 4) {
        simulation.timeScale = stepped(simulation.timeScale, kProjectSettingsTimeScaleIncrement, 0.0F, kb::project::kProjectTimeScaleMax);
    }
    return simulation;
}

[[nodiscard]] ProjectSettingsTooltipKind TooltipKindForHit(ProjectSettingsPanelRenderer::Hit hit) noexcept {
    switch (hit.kind) {
    case ProjectSettingsHitKind::WindowModeOption:
        return ProjectSettingsTooltipKind::WindowMode;
    case ProjectSettingsHitKind::WindowSizeOption:
        return ProjectSettingsTooltipKind::WindowSize;
    case ProjectSettingsHitKind::DisplayToggle:
        return hit.index == 0 ? ProjectSettingsTooltipKind::Resizable : ProjectSettingsTooltipKind::VSync;
    case ProjectSettingsHitKind::FrameRateOption:
        return ProjectSettingsTooltipKind::FrameRateLimit;
    case ProjectSettingsHitKind::SimulationStepper: {
        const int stepper = hit.index / 2;
        return stepper == 0 ? ProjectSettingsTooltipKind::FixedStep
            : (stepper == 4 ? ProjectSettingsTooltipKind::TimeScale : ProjectSettingsTooltipKind::Gravity);
    }
    case ProjectSettingsHitKind::GravityPresetOption:
        return ProjectSettingsTooltipKind::GravityPreset;
    case ProjectSettingsHitKind::MappingContextField:
        return ProjectSettingsTooltipKind::MappingContext;
    case ProjectSettingsHitKind::PhysicsLayersField:
        return ProjectSettingsTooltipKind::PhysicsLayers;
    case ProjectSettingsHitKind::EnabledCheckbox:
        return ProjectSettingsTooltipKind::InputEnabled;
    case ProjectSettingsHitKind::RenderBackendOption:
        return ProjectSettingsTooltipKind::RenderBackend;
    case ProjectSettingsHitKind::LightingPathOption:
        return ProjectSettingsTooltipKind::LightingPath;
    case ProjectSettingsHitKind::AntiAliasingMode:
        return ProjectSettingsTooltipKind::AntiAliasing;
    case ProjectSettingsHitKind::MsaaOption:
        return ProjectSettingsTooltipKind::MsaaSamples;
    case ProjectSettingsHitKind::GraphicsToggle:
        return GraphicsToggleTooltipKind(hit.index);
    case ProjectSettingsHitKind::QualityOption:
        return QualityTooltipKind(hit.index / 5);
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
    EditorRenderBackendSettings* renderBackendSettings,
    const RECT& content,
    int x,
    int y) {
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::HitTest(content, sceneContext, x, y);
    // A click in the search box starts typing there; anywhere else in the panel ends it.
    sceneContext.ProjectSettings().Search().Focus(hit.kind == ProjectSettingsHitKind::SearchField);
    switch (hit.kind) {
    case ProjectSettingsHitKind::SearchField:
        return true;
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
    case ProjectSettingsHitKind::LightingPathOption: {
        const bool changed = sceneContext.SetProjectSceneLightingPath(LightingPathForOption(hit.index));
        if (changed) {
            sceneContext.MarkSceneRenderDirty();
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    }
    case ProjectSettingsHitKind::QualityOption: {
        // A click always lands on a level button here; the label cells only answer hover tooltips.
        static_cast<void>(sceneContext.SetProjectQualityLevel(hit.index / 5, static_cast<kb::project::ProjectQualityLevel>(hit.index % 5)));
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    }
    case ProjectSettingsHitKind::WindowModeOption:
    case ProjectSettingsHitKind::WindowSizeOption:
    case ProjectSettingsHitKind::DisplayToggle:
    case ProjectSettingsHitKind::FrameRateOption: {
        // A click on a label cell only answers the tooltip.
        kb::project::ProjectDisplaySettings display = sceneContext.ProjectConfiguration().display;
        if (hit.index >= 0) {
            switch (hit.kind) {
            case ProjectSettingsHitKind::WindowModeOption:
                display.windowMode = hit.index == 0 ? kb::project::ProjectWindowMode::Windowed
                                                    : kb::project::ProjectWindowMode::BorderlessFullscreen;
                break;
            case ProjectSettingsHitKind::WindowSizeOption:
                display.windowWidth = kProjectSettingsWindowSizes[static_cast<std::size_t>(hit.index)].width;
                display.windowHeight = kProjectSettingsWindowSizes[static_cast<std::size_t>(hit.index)].height;
                break;
            case ProjectSettingsHitKind::DisplayToggle: {
                bool& flag = hit.index == 0 ? display.resizable : display.vsync;
                flag = !flag;
                break;
            }
            default:
                display.frameRateCap = kProjectSettingsFrameRates[static_cast<std::size_t>(hit.index)];
                break;
            }
            static_cast<void>(sceneContext.SetProjectDisplay(display));
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    }
    case ProjectSettingsHitKind::SimulationStepper:
    case ProjectSettingsHitKind::GravityPresetOption: {
        kb::project::ProjectSimulationSettings simulation = sceneContext.ProjectConfiguration().simulation;
        if (hit.index >= 0) {
            if (hit.kind == ProjectSettingsHitKind::GravityPresetOption) {
                simulation.gravity = kProjectSettingsGravityPresets[static_cast<std::size_t>(hit.index)];
            } else {
                simulation = SteppedSimulation(simulation, hit.index / 2, hit.index % 2);
            }
            static_cast<void>(sceneContext.SetProjectSimulation(simulation));
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
        const EditorAntiAliasingMode previousMode = renderBackendSettings->AntiAliasingMode();
        const std::uint64_t previousGeneration = renderBackendSettings->Generation();
        const std::uint64_t previousBackendGeneration = renderBackendSettings->BackendGeneration();
        const EditorAntiAliasingMode requestedMode = AntiAliasingModeForOption(hit.index);
        renderBackendSettings->SetAntiAliasingMode(requestedMode);
        {
            std::ostringstream message;
            message << "UI AA mode click index=" << hit.index
                    << " requested=" << AntiAliasingModeName(requestedMode)
                    << " previous=" << AntiAliasingModeName(previousMode)
                    << " current=" << AntiAliasingModeName(renderBackendSettings->AntiAliasingMode())
                    << " fxaa=" << BoolText(renderBackendSettings->FxaaEnabled())
                    << " taa=" << BoolText(renderBackendSettings->TemporalAntiAliasingEnabled())
                    << " msaaSamples=" << static_cast<unsigned>(renderBackendSettings->MsaaSamples())
                    << " generation=" << previousGeneration << "->" << renderBackendSettings->Generation()
                    << " backendGeneration=" << previousBackendGeneration << "->" << renderBackendSettings->BackendGeneration();
            EditorCrashBreadcrumbs::Write("aa_trace", message.str());
            sceneContext.Console().Info("AA", message.str());
        }
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
        const std::uint64_t previousBackendGeneration = renderBackendSettings->BackendGeneration();
        const std::uint8_t requestedSamples = MsaaSamplesForOption(hit.index);
        renderBackendSettings->SetMsaaSamples(requestedSamples);
        {
            std::ostringstream message;
            message << "UI MSAA samples click index=" << hit.index
                    << " requestedSamples=" << static_cast<unsigned>(requestedSamples)
                    << " mode=" << AntiAliasingModeName(renderBackendSettings->AntiAliasingMode())
                    << " fxaa=" << BoolText(renderBackendSettings->FxaaEnabled())
                    << " taa=" << BoolText(renderBackendSettings->TemporalAntiAliasingEnabled())
                    << " msaaSamples=" << static_cast<unsigned>(renderBackendSettings->MsaaSamples())
                    << " generation=" << previousGeneration << "->" << renderBackendSettings->Generation()
                    << " backendGeneration=" << previousBackendGeneration << "->" << renderBackendSettings->BackendGeneration();
            EditorCrashBreadcrumbs::Write("aa_trace", message.str());
            sceneContext.Console().Info("AA", message.str());
        }
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
