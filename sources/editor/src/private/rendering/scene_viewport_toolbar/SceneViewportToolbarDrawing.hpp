#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/HeroIconKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <string_view>

namespace kb::editor {

#if defined(_WIN32)

class SceneViewportToolbarDrawing {
public:
    SceneViewportToolbarDrawing() = delete;

    [[nodiscard]] static COLORREF Blend(COLORREF a, COLORREF b, int numerator, int denominator) noexcept;
    [[nodiscard]] static COLORREF ToolbarRowColor(const EditorTheme& theme) noexcept;
    static void FillRound(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius);
    static void DrawIconButton(HDC dc, RECT rect, HeroIconKind icon, const EditorTheme& theme, bool active);
    // An icon button that also names its keyboard shortcut, small, beside the icon.
    static void DrawToolButton(HDC dc, RECT rect, HeroIconKind icon, std::string_view key, const EditorTheme& theme, bool active);
    static void DrawValueButton(HDC dc, RECT rect, HeroIconKind icon, std::string_view value, const EditorTheme& theme, bool active = false, bool dimmed = false);
    static void DrawDivider(HDC dc, const RECT& toolbar, int x, const EditorTheme& theme);
    // A labelled toggle: lit while `active`.
    static void DrawChip(HDC dc, const RECT& rect, std::string_view label, const EditorTheme& theme, bool active);
    // A read-out between the two buttons of a stepper.
    static void DrawValueBox(HDC dc, const RECT& rect, std::string_view text, const EditorTheme& theme);
    // A value with a minus button on its left and a plus button on its right.
    static void DrawStepper(HDC dc, const RECT& minus, const RECT& value, const RECT& plus, std::string_view text, const EditorTheme& theme);
};

#endif

} // namespace kb::editor
