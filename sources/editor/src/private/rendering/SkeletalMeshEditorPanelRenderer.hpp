#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class SkeletalMeshEditorPanelRenderer {
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
    [[nodiscard]] static std::optional<SkeletalMeshEditorTreeRow> TreeRowAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y);
    [[nodiscard]] static bool IsTreeSearchAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<kb::scene::SkeletonBoneId> BoneAt(
        const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept;
#endif
};

} // namespace kb::editor
