#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/AnimatorEditorPanelLayout.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class AnimatorEditorPanelRenderer {
public:
#if defined(_WIN32)
    [[nodiscard]] static AnimatorEditorPanelLayout ResolveLayout(const RECT& content) noexcept {
        return AnimatorEditorPanelLayoutResolver::Resolve(content);
    }
    [[nodiscard]] static std::optional<std::uint64_t> GraphStateAt(
        const RECT& content, const kb::scene::AnimatorController& controller, int x, int y) noexcept;
    void Paint(
        HDC dc,
        HWND host,
        const RECT& content,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings,
        EditorSceneBgfxViewport* sceneViewport) const;
#endif
};

} // namespace kb::editor
