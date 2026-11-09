#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <optional>
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
};

// Categories shown in the left sidebar. Add entries here as the panel grows;
// each maps to a content page rendered in the right pane.
enum class ProjectSettingsCategory : int {
    // Project
    Inputs = 0,
    Graphics,
    Physics,
    Lighting,
    // Editor: the Editor Settings pages, shown in this window.
    EditorGeneral,
    EditorSaving,
    // Plugins: the Plugins list, shown in this window.
    Plugins,
    Count,
};
#endif

#if defined(_WIN32)
static_assert(static_cast<int>(ProjectSettingsCategory::EditorGeneral) == 4 && static_cast<int>(ProjectSettingsCategory::Plugins) == 6,
    "the sidebar's group captions (ProjectSettingsPanelLayout::k*GroupStart) follow the category order");
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
