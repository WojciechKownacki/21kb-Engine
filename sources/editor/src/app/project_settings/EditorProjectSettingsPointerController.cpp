#include "app/project_settings/EditorProjectSettingsPointerController.hpp"

#if defined(_WIN32)
#include "app/EditorCrashBreadcrumbs.hpp"
#include "localization/EditorLocalization.hpp"
#include "rendering/ProjectSettingsPanelRenderer.hpp"
#include "platform/win32/Utf8Gdi.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
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
    case 0:
        return kb::project::ProjectSceneLightingPath::Auto;
    case 2:
        return kb::project::ProjectSceneLightingPath::ForwardPlus;
    case 3:
        return kb::project::ProjectSceneLightingPath::Deferred;
    case 1:
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

// The master volume after one click of its minus (direction 0) or plus (1); it stays inside 0..1 with two decimals.
[[nodiscard]] float SteppedMasterVolume(float volume, int direction) noexcept {
    const float sign = direction == 0 ? -1.0F : 1.0F;
    return std::clamp(std::round((volume + sign * kProjectSettingsMasterVolumeIncrement) * 100.0F) / 100.0F, 0.0F, 1.0F);
}

[[nodiscard]] ProjectSettingsTooltipKind TooltipKindForHit(ProjectSettingsPanelRenderer::Hit hit) noexcept {
    switch (hit.kind) {
    case ProjectSettingsHitKind::MasterVolumeStepper:
        return ProjectSettingsTooltipKind::MasterVolume;
    case ProjectSettingsHitKind::MicrophoneToggle:
        return ProjectSettingsTooltipKind::Microphone;
    case ProjectSettingsHitKind::TagNameField:
        return ProjectSettingsTooltipKind::Tags;
    case ProjectSettingsHitKind::NavAreaNameField:
        return ProjectSettingsTooltipKind::NavigationAreas;
    case ProjectSettingsHitKind::WindowModeOption:
        return ProjectSettingsTooltipKind::WindowMode;
    case ProjectSettingsHitKind::WindowSizeOption:
        return ProjectSettingsTooltipKind::WindowSize;
    case ProjectSettingsHitKind::DisplayToggle:
        return hit.index == 0 ? ProjectSettingsTooltipKind::Resizable
            : hit.index == 1 ? ProjectSettingsTooltipKind::VSync : ProjectSettingsTooltipKind::SplashProgress;
    case ProjectSettingsHitKind::FrameRateOption:
        return ProjectSettingsTooltipKind::FrameRateLimit;
    case ProjectSettingsHitKind::AndroidGraphicsApiOption:
        return ProjectSettingsTooltipKind::AndroidGraphicsApi;
    case ProjectSettingsHitKind::SplashImageField:
        return ProjectSettingsTooltipKind::SplashImage;
    case ProjectSettingsHitKind::SplashScaleOption:
        return ProjectSettingsTooltipKind::SplashScale;
    case ProjectSettingsHitKind::SplashColorOption:
        return ProjectSettingsTooltipKind::SplashBackground;
    case ProjectSettingsHitKind::SplashMinimumOption:
        return ProjectSettingsTooltipKind::SplashMinimumTime;
    case ProjectSettingsHitKind::SimulationStepper: {
        const int stepper = hit.index / 2;
        return stepper == 0 ? ProjectSettingsTooltipKind::FixedStep
            : (stepper == 4 ? ProjectSettingsTooltipKind::TimeScale : ProjectSettingsTooltipKind::Gravity);
    }
    case ProjectSettingsHitKind::GravityPresetOption:
        return ProjectSettingsTooltipKind::GravityPreset;
    case ProjectSettingsHitKind::NumberField:
        switch (static_cast<ProjectSettingsNumber>(hit.index)) {
        case ProjectSettingsNumber::FixedStep:
            return ProjectSettingsTooltipKind::FixedStep;
        case ProjectSettingsNumber::GravityX:
        case ProjectSettingsNumber::GravityY:
        case ProjectSettingsNumber::GravityZ:
            return ProjectSettingsTooltipKind::Gravity;
        case ProjectSettingsNumber::TimeScale:
            return ProjectSettingsTooltipKind::TimeScale;
        case ProjectSettingsNumber::WindowWidth:
        case ProjectSettingsNumber::WindowHeight:
            return ProjectSettingsTooltipKind::WindowSize;
        case ProjectSettingsNumber::FrameRateCap:
            return ProjectSettingsTooltipKind::FrameRateLimit;
        case ProjectSettingsNumber::MasterVolume:
            return ProjectSettingsTooltipKind::MasterVolume;
        case ProjectSettingsNumber::RenderScale:
            return ProjectSettingsTooltipKind::RenderScale;
        case ProjectSettingsNumber::Count:
        default:
            return ProjectSettingsTooltipKind::None;
        }
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
    case ProjectSettingsHitKind::SplashImageOption:
    case ProjectSettingsHitKind::SearchField:
    case ProjectSettingsHitKind::BuildSceneMoveUp:
    case ProjectSettingsHitKind::BuildSceneRemove:
    case ProjectSettingsHitKind::BuildSceneAdd:
    case ProjectSettingsHitKind::TagAdd:
    case ProjectSettingsHitKind::TagRemove:
    default:
        return ProjectSettingsTooltipKind::None;
    }
}

// The number a person typed: digits with an optional minus and a decimal point or comma; nothing else.
[[nodiscard]] std::optional<double> ParseTypedNumber(std::string text) {
    std::ranges::replace(text, ',', '.');
    double value = 0.0;
    const char* const end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

// The colours an area's swatch steps through, and the colour a new area starts with.
constexpr std::array<std::uint32_t, 10U> kNavAreaColours{ 0x8CFF00U, 0xB0B0B0U, 0x2E9E3EU, 0x2F80EDU, 0xE03C31U,
    0xF2C94CU, 0x9B51E0U, 0xF2994AU, 0x56CCF2U, 0xEB5FA8U };

[[nodiscard]] std::uint32_t NextNavAreaColour(std::uint32_t colour) noexcept {
    const auto found = std::ranges::find(kNavAreaColours, colour);
    return found == kNavAreaColours.end() || found + 1 == kNavAreaColours.end() ? kNavAreaColours.front() : *(found + 1);
}

// The cell of the Navigation page a hit falls on, as the edit it starts; -1 for anything else.
[[nodiscard]] int NavAreaEditForHit(const ProjectSettingsPanelRenderer::Hit& hit) noexcept {
    switch (hit.kind) {
    case ProjectSettingsHitKind::NavAreaNameField: return EditorProjectSettingsState::kNavAreaNameEdit;
    case ProjectSettingsHitKind::NavAreaName: return EditorProjectSettingsState::kNavAreaRenameEdit + hit.index;
    case ProjectSettingsHitKind::NavAreaCost: return EditorProjectSettingsState::kNavAreaCostEdit + hit.index;
    default: return -1;
    }
}

// Applies a typed navigation area cell: a new area's name adds the area, an area's name renames it and its cost sets
// it. False when the edit is not one of these.
bool CommitNavAreaEdit(EditorSceneContext& sceneContext, int edit, std::string text) {
    std::vector<kb::scene::NavAreaDefinition> areas = sceneContext.ProjectConfiguration().navigationAreas;
    const auto trimmed = [](std::string value) {
        const std::size_t begin = value.find_first_not_of(' ');
        const std::size_t end = value.find_last_not_of(' ');
        return begin == std::string::npos ? std::string{} : value.substr(begin, end - begin + 1U);
    };
    if (edit == EditorProjectSettingsState::kNavAreaNameEdit) {
        areas.push_back(kb::scene::NavAreaDefinition{ .name = trimmed(std::move(text)), .colour = kNavAreaColours[areas.size() % kNavAreaColours.size()], .cost = 1.0F });
    } else if (const int rename = edit - EditorProjectSettingsState::kNavAreaRenameEdit;
               rename >= 0 && rename < EditorProjectSettingsState::kNavAreaEditRange) {
        if (static_cast<std::size_t>(rename) >= areas.size()) return true;
        areas[static_cast<std::size_t>(rename)].name = trimmed(std::move(text));
    } else if (const int cost = edit - EditorProjectSettingsState::kNavAreaCostEdit;
               cost >= 0 && cost < EditorProjectSettingsState::kNavAreaEditRange) {
        const std::optional<double> value = ParseTypedNumber(std::move(text));
        if (static_cast<std::size_t>(cost) >= areas.size() || !value.has_value()) return true;
        areas[static_cast<std::size_t>(cost)].cost = static_cast<float>(*value);
    } else {
        return false;
    }
    // A list that cannot be the project's (an empty or repeated name, a negative cost) leaves the areas as they were.
    static_cast<void>(sceneContext.SetProjectNavigationAreas(std::move(areas)));
    return true;
}

// Offers the project's scenes that are not in the build list yet and appends the chosen one.
[[nodiscard]] bool AddSceneFromMenu(EditorSceneContext& sceneContext, HWND owner) {
    if (owner == nullptr) {
        return false;
    }
    const std::vector<std::string> listed = kb::project::ProjectBuildScenes(sceneContext.ProjectConfiguration());
    std::vector<std::string> choices;
    for (const std::string& scene : sceneContext.ProjectScenePaths()) {
        if (std::ranges::find(listed, scene) == listed.end()) {
            choices.push_back(scene);
        }
    }
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return false;
    }
    if (choices.empty()) {
        AppendMenuUtf8(menu, MF_STRING | MF_GRAYED, 0U, EditorText("projectSettings.sceneNoneToAdd", "Every scene of this project is in the build").data());
    }
    for (std::size_t index = 0; index < choices.size(); ++index) {
        AppendMenuUtf8(menu, MF_STRING, static_cast<UINT_PTR>(index + 1U), choices[index].c_str());
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, cursor.x, cursor.y, 0, owner, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0 || static_cast<std::size_t>(chosen) > choices.size()) {
        return false;
    }
    std::vector<std::string> next = listed;
    next.push_back(choices[static_cast<std::size_t>(chosen - 1)]);
    return sceneContext.SetProjectBuildScenes(std::move(next));
}

[[nodiscard]] bool HandleProjectSettingsPointerDown(
    EditorSceneContext& sceneContext,
    EditorRenderBackendSettings* renderBackendSettings,
    const RECT& content,
    int x,
    int y,
    HWND owner) {
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::HitTest(content, sceneContext, x, y);
    // A click in the search box starts typing there; anywhere else in the panel ends it.
    sceneContext.ProjectSettings().Search().Focus(hit.kind == ProjectSettingsHitKind::SearchField);
    // A click outside the number (or the tag name) being typed applies it first.
    const EditorProjectSettingsState& settingsState = sceneContext.ProjectSettings();
    const bool staysInCell = (hit.kind == ProjectSettingsHitKind::NumberField && hit.index == settingsState.EditedNumber()) ||
        (hit.kind == ProjectSettingsHitKind::TagNameField && settingsState.IsTagNameEditing()) ||
        (NavAreaEditForHit(hit) >= 0 && NavAreaEditForHit(hit) == settingsState.EditedNumber());
    const bool committed = settingsState.IsNumberEditing() && !staysInCell &&
        EditorProjectSettingsPointerController::CommitNumberEdit(sceneContext);
    switch (hit.kind) {
    case ProjectSettingsHitKind::SearchField:
        return true;
    case ProjectSettingsHitKind::NumberField: {
        // A click on a label cell only answers the tooltip.
        if (hit.index >= 0 && !sceneContext.ProjectSettings().IsNumberEditing()) {
            sceneContext.ProjectSettings().BeginNumberEdit(hit.index,
                ProjectSettingsPanelRenderer::NumberText(sceneContext.ProjectConfiguration(), static_cast<ProjectSettingsNumber>(hit.index)));
        }
        return true;
    }
    case ProjectSettingsHitKind::BuildSceneMoveUp:
    case ProjectSettingsHitKind::BuildSceneRemove: {
        std::vector<std::string> scenes = kb::project::ProjectBuildScenes(sceneContext.ProjectConfiguration());
        const auto index = static_cast<std::size_t>(std::max(hit.index, 0));
        if (index < scenes.size()) {
            if (hit.kind == ProjectSettingsHitKind::BuildSceneRemove) {
                scenes.erase(scenes.begin() + static_cast<std::ptrdiff_t>(index));
            } else if (index > 0U) {
                std::swap(scenes[index], scenes[index - 1U]);
            }
            static_cast<void>(sceneContext.SetProjectBuildScenes(std::move(scenes)));
        }
        return true;
    }
    case ProjectSettingsHitKind::BuildSceneAdd:
        static_cast<void>(AddSceneFromMenu(sceneContext, owner));
        return true;
    case ProjectSettingsHitKind::TagNameField:
        if (!sceneContext.ProjectSettings().IsTagNameEditing()) {
            sceneContext.ProjectSettings().BeginTagNameEdit();
        }
        return true;
    case ProjectSettingsHitKind::TagAdd:
        // A typed name was added above; the cell is ready for the next one.
        sceneContext.ProjectSettings().BeginTagNameEdit();
        return true;
    case ProjectSettingsHitKind::NavAreaNameField:
    case ProjectSettingsHitKind::NavAreaName:
    case ProjectSettingsHitKind::NavAreaCost: {
        const std::vector<kb::scene::NavAreaDefinition>& areas = sceneContext.ProjectConfiguration().navigationAreas;
        const int edit = NavAreaEditForHit(hit);
        if (sceneContext.ProjectSettings().EditedNumber() != edit) {
            const auto area = static_cast<std::size_t>(std::max(hit.index, 0));
            sceneContext.ProjectSettings().BeginNumberEdit(edit, hit.kind == ProjectSettingsHitKind::NavAreaNameField || area >= areas.size() ? std::string{}
                : hit.kind == ProjectSettingsHitKind::NavAreaName ? areas[area].name : ProjectSettingsPanelRenderer::NavAreaCostText(areas[area].cost));
        }
        return true;
    }
    case ProjectSettingsHitKind::NavAreaAdd:
        // A typed name was added above; the cell is ready for the next one.
        sceneContext.ProjectSettings().BeginNumberEdit(EditorProjectSettingsState::kNavAreaNameEdit, {});
        return true;
    case ProjectSettingsHitKind::NavAreaColour: {
        std::vector<kb::scene::NavAreaDefinition> areas = sceneContext.ProjectConfiguration().navigationAreas;
        if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < areas.size()) {
            areas[static_cast<std::size_t>(hit.index)].colour = NextNavAreaColour(areas[static_cast<std::size_t>(hit.index)].colour);
            static_cast<void>(sceneContext.SetProjectNavigationAreas(std::move(areas)));
        }
        return true;
    }
    case ProjectSettingsHitKind::NavAreaRemove: {
        // Only the last area goes, so the ids scenes store for the others keep their meaning.
        std::vector<kb::scene::NavAreaDefinition> areas = sceneContext.ProjectConfiguration().navigationAreas;
        if (areas.size() > 1U && hit.index + 1 == static_cast<int>(areas.size())) {
            areas.pop_back();
            static_cast<void>(sceneContext.SetProjectNavigationAreas(std::move(areas)));
        }
        return true;
    }
    case ProjectSettingsHitKind::TagRemove: {
        const std::vector<std::string> tags = sceneContext.ProjectConfiguration().tags;
        if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < tags.size()) {
            static_cast<void>(sceneContext.DeleteSceneTag(tags[static_cast<std::size_t>(hit.index)]));
        }
        return true;
    }
    case ProjectSettingsHitKind::MasterVolumeStepper:
        if (hit.index >= 0) {
            static_cast<void>(sceneContext.SetProjectMasterVolume(SteppedMasterVolume(sceneContext.ProjectConfiguration().masterVolume, hit.index)));
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
        return true;
    case ProjectSettingsHitKind::MicrophoneToggle:
        if (hit.index >= 0) {
            static_cast<void>(sceneContext.SetProjectMicrophone(!sceneContext.ProjectConfiguration().microphone));
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
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
    case ProjectSettingsHitKind::SplashImageField:
        sceneContext.ProjectSettings().ToggleSplashImageDropdown();
        return true;
    case ProjectSettingsHitKind::SplashImageOption: {
        const std::vector<std::string> options = sceneContext.ProjectSplashImageOptions();
        if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < options.size()) {
            kb::project::ProjectDisplaySettings display = sceneContext.ProjectConfiguration().display;
            display.splash.image = options[static_cast<std::size_t>(hit.index)];
            static_cast<void>(sceneContext.SetProjectDisplay(display));
        }
        static_cast<void>(sceneContext.CloseProjectSettingsDropdowns());
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
    case ProjectSettingsHitKind::FrameRateOption:
    case ProjectSettingsHitKind::AndroidGraphicsApiOption:
    case ProjectSettingsHitKind::SplashScaleOption:
    case ProjectSettingsHitKind::SplashColorOption:
    case ProjectSettingsHitKind::SplashMinimumOption: {
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
                bool& flag = hit.index == 0 ? display.resizable : hit.index == 1 ? display.vsync : display.splash.showProgress;
                flag = !flag;
                break;
            }
            case ProjectSettingsHitKind::SplashScaleOption:
                display.splash.scale = static_cast<kb::project::ProjectSplashScale>(hit.index);
                break;
            case ProjectSettingsHitKind::SplashColorOption:
                display.splash.backgroundColor = kProjectSettingsSplashColors[static_cast<std::size_t>(hit.index)];
                break;
            case ProjectSettingsHitKind::SplashMinimumOption:
                display.splash.minimumDisplayMilliseconds = kProjectSettingsSplashMinimumTimes[static_cast<std::size_t>(hit.index)];
                break;
            case ProjectSettingsHitKind::AndroidGraphicsApiOption:
                display.androidGraphicsApi = static_cast<kb::project::ProjectAndroidGraphicsApi>(hit.index);
                break;
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
        return sceneContext.CloseProjectSettingsDropdowns() || committed;
    }
}

} // namespace

bool EditorProjectSettingsPointerController::HandlePointerDown(const RECT& content, int x, int y) {
    return HandleProjectSettingsPointerDown(sceneContext_, nullptr, content, x, y, nullptr);
}

bool EditorProjectSettingsPointerController::HandlePointerDown(const RECT& content, int x, int y, EditorRenderBackendSettings& renderBackendSettings, HWND owner) {
    return HandleProjectSettingsPointerDown(sceneContext_, &renderBackendSettings, content, x, y, owner);
}

bool EditorProjectSettingsPointerController::CommitNumberEdit(EditorSceneContext& sceneContext) {
    EditorProjectSettingsState& state = sceneContext.ProjectSettings();
    if (!state.IsNumberEditing()) {
        return false;
    }
    const std::string text{ state.NumberEdit().Query() };
    if (const int edit = state.EditedNumber(); edit >= EditorProjectSettingsState::kNavAreaNameEdit && edit != EditorProjectSettingsState::kTagNameEdit) {
        state.EndNumberEdit();
        if (CommitNavAreaEdit(sceneContext, edit, text)) return true;
    }
    if (state.IsTagNameEditing()) {
        // A typed tag name is added to the project's list (a name that cannot be listed is dropped).
        state.EndNumberEdit();
        static_cast<void>(sceneContext.AddProjectTag(text));
        return true;
    }
    const auto number = static_cast<ProjectSettingsNumber>(state.EditedNumber());
    state.EndNumberEdit();
    if (const std::optional<double> value = ParseTypedNumber(text); value.has_value()) {
        const kb::project::ProjectSettings& current = sceneContext.ProjectConfiguration();
        const kb::project::ProjectSettings next = ProjectSettingsPanelRenderer::WithNumber(current, number, *value);
        if (next.display != current.display) {
            static_cast<void>(sceneContext.SetProjectDisplay(next.display));
        }
        if (next.simulation != current.simulation) {
            static_cast<void>(sceneContext.SetProjectSimulation(next.simulation));
        }
        if (next.masterVolume != current.masterVolume) {
            static_cast<void>(sceneContext.SetProjectMasterVolume(next.masterVolume));
        }
    }
    return true;
}

bool EditorProjectSettingsPointerController::UpdateHover(const RECT& content, int x, int y) {
    bool changed = false;
    const ProjectSettingsPanelRenderer::Hit hit = ProjectSettingsPanelRenderer::TooltipHitTest(content, sceneContext_, x, y);
    changed = sceneContext_.ProjectSettings().SetTooltip(TooltipKindForHit(hit), x, y) || changed;
    if (!sceneContext_.ProjectSettings().IsMappingContextDropdownOpen() && !sceneContext_.ProjectSettings().IsPhysicsLayersDropdownOpen() &&
        !sceneContext_.ProjectSettings().IsSplashImageDropdownOpen()) {
        return sceneContext_.ProjectSettings().SetHoveredOption(-1) || changed;
    }
    const int hovered = (hit.kind == ProjectSettingsHitKind::MappingContextOption || hit.kind == ProjectSettingsHitKind::PhysicsLayersOption ||
        hit.kind == ProjectSettingsHitKind::SplashImageOption) ? hit.index : -1;
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
