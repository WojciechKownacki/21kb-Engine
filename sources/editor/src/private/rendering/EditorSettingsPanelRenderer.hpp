#pragma once

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

class EditorSceneContext;
class EditorRenderBackendSettings;

#if defined(_WIN32)
enum class EditorSettingsHitKind : std::uint8_t {
    None,
    Category,
    Toggle,
    Choice,
};

struct EditorSettingsHit {
    EditorSettingsHitKind kind = EditorSettingsHitKind::None;
    int row = -1;
    int option = -1;
};
#endif

class EditorSettingsPanelRenderer final {
public:
#if defined(_WIN32)
    void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderer) const;
    [[nodiscard]] static EditorSettingsHit HitTest(
        const RECT& content,
        const EditorSceneContext& sceneContext,
        int x,
        int y) noexcept;
#endif
};

} // namespace kb::editor
