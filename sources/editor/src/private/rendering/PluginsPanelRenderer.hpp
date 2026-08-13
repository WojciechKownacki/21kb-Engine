#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <cstddef>

namespace kb::editor {

#if defined(_WIN32)
enum class PluginsPanelHitKind : std::uint8_t {
    None,
    Toggle,
    Row,
    ScrollbarThumb,
    ScrollbarTrack,
    ParticleProviderAdd,
    ParticleProviderCancel,
};
#endif

class PluginsPanelRenderer {
public:
#if defined(_WIN32)
    struct Hit {
        PluginsPanelHitKind kind = PluginsPanelHitKind::None;
        std::size_t index = 0;
        RECT rect{};
    };

    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;
    [[nodiscard]] static Hit HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static std::int64_t MaxScrollOffset(const RECT& content) noexcept;
    [[nodiscard]] static int ScrollbarTrackTravel(const RECT& content) noexcept;
#endif
};

} // namespace kb::editor
