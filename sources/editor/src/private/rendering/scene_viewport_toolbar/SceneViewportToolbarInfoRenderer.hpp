#pragma once

#include "rendering/SceneViewportToolbarRenderer.hpp"

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarInfoRenderer {
public:
    SceneViewportToolbarInfoRenderer() = delete;

    static void PaintFpsCounter(HDC dc, RECT rect, const EditorTheme& theme, bool profilerWindowOpen);
};

#endif

} // namespace kb::editor
