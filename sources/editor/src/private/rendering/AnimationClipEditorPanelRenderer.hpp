#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#include <cstdint>
#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class AnimationClipEditorPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(
        HDC dc,
        HWND host,
        const RECT& content,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings,
        EditorSceneBgfxViewport* sceneViewport) const;
    [[nodiscard]] static std::optional<std::uint8_t> TransportControlAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<float> TimelineTimeAt(const RECT& content, const AnimationClipTimelineState& timeline, int x, int y) noexcept;
#endif
};

} // namespace kb::editor
