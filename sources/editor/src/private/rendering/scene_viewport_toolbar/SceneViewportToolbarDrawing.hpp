#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarDrawing {
public:
    SceneViewportToolbarDrawing() = delete;

    [[nodiscard]] static COLORREF Blend(COLORREF a, COLORREF b, int numerator, int denominator) noexcept;
    [[nodiscard]] static COLORREF ToolbarRowColor(const EditorTheme& theme) noexcept;
    static void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius);
    static void DrawIconButton(HDC dc, RECT rect, HeroIconKind icon, const EditorTheme& theme, bool active);
    static void DrawValueButton(HDC dc, RECT rect, HeroIconKind icon, const char* value, const EditorTheme& theme, bool active = false);
    static void DrawDivider(HDC dc, const RECT& toolbar, int x, const EditorTheme& theme);
};

#endif

} // namespace kb::editor
