#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kb::editor {

#if defined(_WIN32)
// Interactive controls exposed by the Project Settings panel. The panel is
// intentionally minimal (a plain "Project Settings > Input" page): the active mapping
// context is a project-wide setting, not a per-entity component.
enum class ProjectSettingsHitKind : std::uint8_t {
    None,
    CategoryItem,         // A row in the left category sidebar (see Hit::index).
    MappingContextField,  // The closed selector box (click toggles the dropdown).
    MappingContextOption, // A row inside the open dropdown list (see Hit::index).
    PhysicsLayersField,
    PhysicsLayersOption,
    EnabledCheckbox,
    RenderBackendOption,
    LightingPathOption,
    AntiAliasingMode,
    GraphicsToggle,
    MsaaOption,
    QualityOption,        // A level button of a Lighting row (index = row * 5 + level; level 0 when over the label).
    SearchField,          // The search box above the categories.
    WindowModeOption,     // Display: Windowed (0) or Borderless (1); -1 over the label.
    WindowSizeOption,     // Display: one of the size presets; -1 over the label.
    DisplayToggle,        // Display: Resizable (0) or VSync (1).
    FrameRateOption,      // Display: one of the frame rate limits; -1 over the label.
    AndroidGraphicsApiOption, // Display: Auto (0), Vulkan (1) or OpenGL ES (2); -1 over the label.
    SplashScaleOption,    // Display: how the launch screen image fits: Fit (0), Fill (1) or Center (2); -1 over the label.
    SplashColorOption,    // Display: one of the launch screen background colours; -1 over the label.
    SplashMinimumOption,  // Display: one of the launch screen minimum display times; -1 over the label.
    SplashImageField,     // Display: the closed launch screen image selector (click toggles its list); also its label.
    SplashImageOption,    // Display: a row inside the open image list (see Hit::index).
    SimulationStepper,    // Physics: index = stepper * 2 + (0 minus, 1 plus); stepper 0 fixed step, 1..3 gravity, 4 time scale. -1 over a label.
    GravityPresetOption,  // Physics: one of the gravity presets; -1 over the label.
    NumberField,          // A value cell that takes a typed number (index = ProjectSettingsNumber); -1 over its label.
    BuildSceneMoveUp,     // Scenes: moves a scene one place up the build list (index = position in the list).
    BuildSceneRemove,     // Scenes: takes a scene out of the build list (index = position in the list).
    BuildSceneAdd,        // Scenes: the button that offers the project's scenes that are not in the list yet.
    MasterVolumeStepper,  // Audio: index 0 minus, 1 plus; -1 over the label.
    MicrophoneToggle,     // Audio: the microphone checkbox; -1 over the label.
    TagNameField,         // Tags: the cell that takes the name of a new tag.
    TagAdd,               // Tags: the button that adds the typed tag.
    TagRemove,            // Tags: takes a tag out of the project's list (index = position in the list).
    NavAreaNameField,     // Navigation: the cell that takes the name of a new area.
    NavAreaAdd,           // Navigation: the button that adds the typed area.
    NavAreaName,          // Navigation: an area's name, typed anew (index = area id).
    NavAreaColour,        // Navigation: an area's colour swatch; a click picks the next colour (index = area id).
    NavAreaCost,          // Navigation: an area's cost cell (index = area id).
    NavAreaRemove,        // Navigation: takes the last area out of the list (index = area id).
};

// Categories shown in the left sidebar. Add entries here as the panel grows;
// each maps to a content page rendered in the right pane.
enum class ProjectSettingsCategory : int {
    // Project
    Inputs = 0,
    Graphics,
    Physics,
    Lighting,
    Display,
    Audio,
    Scenes,
    Tags,
    Navigation,
    // Editor: the Editor Settings pages, shown in this window.
    EditorGeneral,
    EditorSaving,
    // Plugins: the Plugins list, shown in this window.
    Plugins,
    Count,
};
#endif

#if defined(_WIN32)
static_assert(static_cast<int>(ProjectSettingsCategory::EditorGeneral) == 9 && static_cast<int>(ProjectSettingsCategory::Plugins) == 11,
    "the sidebar's group captions (ProjectSettingsPanelLayout::k*GroupStart) follow the category order");
#endif

#if defined(_WIN32)
struct ProjectSettingsPanelLayoutRects;

// The choices of the Display and Physics pages, shared by painting and by the clicks that apply them.
struct ProjectSettingsWindowSize {
    std::uint32_t width;
    std::uint32_t height;
};
inline constexpr std::array<ProjectSettingsWindowSize, 4U> kProjectSettingsWindowSizes{ {
    { 1280U, 720U }, { 1600U, 900U }, { 1920U, 1080U }, { 2560U, 1440U } } };
// 0 is "no limit".
inline constexpr std::array<std::uint32_t, 5U> kProjectSettingsFrameRates{ 0U, 30U, 60U, 120U, 144U };
// The launch screen background colours and the minimum display times (milliseconds) the Display page offers.
inline constexpr std::array<std::array<float, 3U>, 6U> kProjectSettingsSplashColors{ {
    { 0.06F, 0.07F, 0.09F }, { 0.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 1.0F },
    { 0.10F, 0.20F, 0.45F }, { 0.10F, 0.30F, 0.20F }, { 0.45F, 0.12F, 0.15F } } };
inline constexpr std::array<std::uint32_t, 5U> kProjectSettingsSplashMinimumTimes{ 0U, 500U, 1000U, 2000U, 3000U };
// Earth, the Moon and none.
inline constexpr std::array<std::array<float, 3U>, 3U> kProjectSettingsGravityPresets{ {
    { 0.0F, -9.81F, 0.0F }, { 0.0F, -1.62F, 0.0F }, { 0.0F, 0.0F, 0.0F } } };
// What one click of a stepper's plus or minus adds: the fixed step (Hz), a gravity axis, the time scale.
inline constexpr std::uint32_t kProjectSettingsFixedStepIncrement = 10U;
inline constexpr float kProjectSettingsGravityIncrement = 0.5F;
inline constexpr float kProjectSettingsTimeScaleIncrement = 0.25F;
inline constexpr float kProjectSettingsMasterVolumeIncrement = 0.1F;

// The numeric settings whose value cell takes a typed number.
enum class ProjectSettingsNumber : int {
    FixedStep = 0,
    GravityX,
    GravityY,
    GravityZ,
    TimeScale,
    WindowWidth,
    WindowHeight,
    FrameRateCap,
    MasterVolume,
    RenderScale,
    Count,
};
// What a number setting accepts: whole numbers or not, and the bounds a typed value is clamped to.
struct ProjectSettingsNumberLimits {
    double minimum;
    double maximum;
    bool whole;
};
#endif

class ProjectSettingsPanelRenderer {
public:
#if defined(_WIN32)
    struct Hit {
        ProjectSettingsHitKind kind = ProjectSettingsHitKind::None;
        int index = -1; // Option index when kind == MappingContextOption.
        RECT rect{};
    };

    void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings) const;
    [[nodiscard]] static Hit HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static Hit TooltipHitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    // A number setting's bounds, its current value as text (what the cell shows and what editing starts
    // from), and the settings with that value replaced - clamped to the bounds and rounded for whole numbers.
    // The first row of the Scenes page's build list, or of the Tags page's tag list, on view, and the furthest the
    // list can scroll (0 when it all fits).
    [[nodiscard]] static int FirstVisibleScene(const EditorSceneContext& sceneContext, const ProjectSettingsPanelLayoutRects& rects);
    [[nodiscard]] static int MaximumFirstVisibleScene(const RECT& content, const EditorSceneContext& sceneContext);
    [[nodiscard]] static ProjectSettingsNumberLimits NumberLimits(ProjectSettingsNumber number) noexcept;
    [[nodiscard]] static std::string NumberText(const kb::project::ProjectSettings& settings, ProjectSettingsNumber number);
    // The cost of a navigation area as its cell shows it and as typing it starts.
    [[nodiscard]] static std::string NavAreaCostText(float cost);
    [[nodiscard]] static kb::project::ProjectSettings WithNumber(
        kb::project::ProjectSettings settings, ProjectSettingsNumber number, double value);
    // The page area when the selected category shows another panel's content (Editor or Plugins),
    // so clicks and the wheel there go to that panel's controller; nothing otherwise.
    [[nodiscard]] static std::optional<RECT> EmbeddedPage(const RECT& content, const EditorSceneContext& sceneContext);
    // Whether any setting of the category matches the search text (always true for empty text),
    // and the first category that does.
    [[nodiscard]] static bool CategoryMatches(int category, std::string_view query);
    [[nodiscard]] static std::optional<int> FirstMatchingCategory(std::string_view query);
#endif
};

} // namespace kb::editor
