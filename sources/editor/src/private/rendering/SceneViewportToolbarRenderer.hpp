#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct SceneViewportToolbarRects {
    RECT toolbar{};
    RECT profileButton{};
    RECT fitButton{};
    RECT cameraButton{};
    RECT renderArea{};
};

class SceneViewportToolbarRenderer {
public:
    SceneViewportToolbarRenderer() = delete;

    static constexpr int Height = 30;

    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content) noexcept;
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state);
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state, const char* cameraLabel);
};

#endif

} // namespace kb::editor
