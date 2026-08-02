#pragma once

#include "rendering/SceneViewportToolbarRenderer.hpp"

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarLayout {
public:
    SceneViewportToolbarLayout() = delete;

    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content) noexcept;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(
        const RECT& content,
        const EditorViewportPreviewState& state,
        bool terrainToolsVisible) noexcept;
    [[nodiscard]] static TerrainViewportToolbarRects ResolveTerrainTools(const RECT& content) noexcept;
};

#endif

} // namespace kb::editor
