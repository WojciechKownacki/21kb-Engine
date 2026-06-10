#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
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
    MappingContextField,
    EnabledCheckbox,
};
#endif

class ProjectSettingsPanelRenderer {
public:
#if defined(_WIN32)
    struct Hit {
        ProjectSettingsHitKind kind = ProjectSettingsHitKind::None;
        RECT rect{};
    };

    void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext) const;
    [[nodiscard]] static Hit HitTest(const RECT& content, int x, int y) noexcept;
#endif
};

} // namespace kb::editor
