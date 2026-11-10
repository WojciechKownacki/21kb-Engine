#pragma once

#include "app/EditorStartupProgress.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Where each part of the startup screen sits at a DPI, in client pixels. Fonts are pixel heights
// at that DPI, so the screen is the same picture at every scale.
struct EditorSplashLayout {
    RECT client{};
    RECT mark{};
    RECT title{};
    RECT stage{};
    RECT percent{};
    RECT track{};
    RECT detail{};
    int markFont = 0;
    int titleFont = 0;
    int stageFont = 0;
    int detailFont = 0;
    int border = 1;
};

// The startup screen: the editor's name and mark, a bar, and the stage and item being loaded.
// The window paints it, and the headless checks paint the same states into images.
class EditorSplashPainter {
public:
    EditorSplashPainter() = delete;

    static constexpr int kLogicalWidth = 560;
    static constexpr int kLogicalHeight = 262;

    [[nodiscard]] static EditorSplashLayout Layout(int dpi) noexcept;
    // Width of the filled part of the bar for `fraction` (0 to 1) of the layout's track.
    [[nodiscard]] static int FillWidth(const EditorSplashLayout& layout, double fraction) noexcept;
    // Draws the whole client of `layout` into `dc`.
    static void Paint(HDC dc, const EditorSplashLayout& layout, const EditorTheme& theme,
        const EditorStartupSnapshot& snapshot);
};

#endif

} // namespace kb::editor
