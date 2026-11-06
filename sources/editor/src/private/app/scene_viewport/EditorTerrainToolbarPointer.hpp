#pragma once

#include "rendering/SceneViewportToolbarRenderer.hpp"

namespace kb::editor {

class EditorSceneContext;

#if defined(_WIN32)

// Clicks on the terrain bar and the drawer under it (New World, Advanced).
class EditorTerrainToolbarPointer final {
public:
    EditorTerrainToolbarPointer() = delete;

    // True when the click was on the terrain bar, its drawer or one of its menus.
    [[nodiscard]] static bool HandleClick(EditorSceneContext& sceneContext, const TerrainViewportToolbarRects& toolbar, int x, int y);
};

#endif

} // namespace kb::editor
