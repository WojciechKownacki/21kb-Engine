#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorTerrainService.hpp"

namespace kb::editor {

#if defined(_WIN32)

// What the drawers show of the selected terrain: its samples across (lit among the resolutions on offer), the splat
// map size (0 without layers), the chunk detail error and the collision step.
struct TerrainDrawerValues {
    std::uint32_t resolution = 0U;
    std::uint32_t splatResolution = 0U;
    float lodPixelError = 0.0F;
    std::uint32_t collisionStep = 0U;
};

// The strip under the terrain bar: the New World wizard or the Advanced settings.
class TerrainToolbarDrawer final {
public:
    TerrainToolbarDrawer() = delete;

    // Places the open drawer's widgets under rects.panel and returns the drawer's bottom edge.
    [[nodiscard]] static int Layout(TerrainViewportToolbarRects& rects, EditorTerrainDrawer drawer) noexcept;
    static void Paint(
        HDC dc, const TerrainViewportToolbarRects& rects, const EditorTheme& theme, const EditorTerrainToolState& tool, const TerrainDrawerValues& values);
};

#endif

} // namespace kb::editor
