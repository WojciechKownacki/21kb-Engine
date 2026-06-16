#pragma once

#include "rendering/SceneViewportToolbarRenderer.hpp"

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarInfoRenderer {
public:
    SceneViewportToolbarInfoRenderer() = delete;

    static void PaintFpsCounter(HDC dc, RECT rect, const EditorTheme& theme);
    static void PaintRenderStats(HDC dc, RECT rect, const EditorTheme& theme);
    static void PaintEcsStats(HDC dc, RECT rect, const EditorTheme& theme);
    static void PaintPipelineStats(HDC dc, RECT rect, const EditorTheme& theme);
    static void PaintTooltip(HDC dc, const RECT& content, const SceneViewportToolbarRects& rects);
};

#endif

} // namespace kb::editor
