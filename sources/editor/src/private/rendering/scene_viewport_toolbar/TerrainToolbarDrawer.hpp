#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorTerrainService.hpp"

namespace kb::editor {

#if defined(_WIN32)

// The strip under the terrain bar: the New World wizard or the Advanced settings.
class TerrainToolbarDrawer final {
public:
    TerrainToolbarDrawer() = delete;

    // Places the open drawer's widgets under rects.panel and returns the drawer's bottom edge.
    [[nodiscard]] static int Layout(TerrainViewportToolbarRects& rects, EditorTerrainDrawer drawer) noexcept;
    // `terrainResolution`: the samples across the selected terrain, lit among the resolutions on offer.
    static void Paint(
        HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, std::uint32_t terrainResolution);
};

#endif

} // namespace kb::editor
