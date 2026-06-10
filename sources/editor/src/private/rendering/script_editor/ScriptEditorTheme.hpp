#pragma once

#include "rendering/script_editor/LuaSyntaxHighlighter.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// VS Code "Dark+" inspired colours and metrics for the script editor. The single
// place that decides how the editor looks, keeping the renderer logic-only.
namespace script_editor_theme {

inline constexpr COLORREF kBackground = RGB(30, 30, 30);
inline constexpr COLORREF kGutterBackground = RGB(30, 30, 30);
inline constexpr COLORREF kGutterText = RGB(133, 133, 133);
inline constexpr COLORREF kGutterActive = RGB(200, 200, 200);
inline constexpr COLORREF kActiveLine = RGB(38, 38, 38);
inline constexpr COLORREF kTextDefault = RGB(212, 212, 212);
inline constexpr COLORREF kSelection = RGB(38, 79, 120);
inline constexpr COLORREF kCaret = RGB(220, 220, 220);
inline constexpr COLORREF kScrollThumb = RGB(77, 77, 77);
inline constexpr COLORREF kScrollThumbHot = RGB(110, 110, 110);

inline constexpr int kFontHeight = 16;
inline constexpr int kLineHeight = 19;
inline constexpr int kTextPadLeft = 8;
inline constexpr int kGutterPadRight = 12;
inline constexpr int kScrollbarWidth = 14;

} // namespace script_editor_theme

[[nodiscard]] inline COLORREF ScriptTokenColor(ScriptTokenKind kind) noexcept {
    switch (kind) {
    case ScriptTokenKind::Keyword:
        return RGB(86, 156, 214);
    case ScriptTokenKind::String:
        return RGB(206, 145, 120);
    case ScriptTokenKind::Comment:
        return RGB(106, 153, 85);
    case ScriptTokenKind::Number:
        return RGB(181, 206, 168);
    case ScriptTokenKind::Function:
        return RGB(220, 220, 170);
    case ScriptTokenKind::Default:
    default:
        return script_editor_theme::kTextDefault;
    }
}

#endif

} // namespace kb::editor
