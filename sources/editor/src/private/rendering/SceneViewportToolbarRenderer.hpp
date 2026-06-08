#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <array>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct SceneViewportToolbarRects {
    RECT toolbar{};
    RECT row{};
    RECT gridToggleButton{};
    RECT gridStepButton{};
    RECT snapToggleButton{};
    RECT snapStepButton{};
    RECT dropdownPanel{};
    std::array<RECT, 6U> dropdownItems{};
    RECT renderArea{};
};

class SceneViewportToolbarRenderer {
public:
    SceneViewportToolbarRenderer() = delete;

    static constexpr int Height = 34;

    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content) noexcept;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept;
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state);
};

#endif

} // namespace kb::editor
