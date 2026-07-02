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

namespace kb::editor {

#if defined(_WIN32)
// Interactive controls exposed by the Project Settings panel. The panel is
// intentionally minimal (UE "Project Settings > Input" feel): the active mapping
// context is a project-wide setting, not a per-entity component.
enum class ProjectSettingsHitKind : std::uint8_t {
    None,
    CategoryItem,         // A row in the left category sidebar (see Hit::index).
    MappingContextField,  // The closed selector box (click toggles the dropdown).
    MappingContextOption, // A row inside the open dropdown list (see Hit::index).
    EnabledCheckbox,
    RenderBackendOption,
    LightingPathOption,
    AntiAliasingMode,
    GraphicsToggle,
    MsaaOption,
};

// Categories shown in the left sidebar. Add entries here as the panel grows;
// each maps to a content page rendered in the right pane.
enum class ProjectSettingsCategory : int {
    Inputs = 0,
    Graphics,
    Count,
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
#endif
};

} // namespace kb::editor
