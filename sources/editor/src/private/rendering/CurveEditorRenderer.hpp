#pragma once

#include "curves/CurveEditorState.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// Draws a CurveEditorState: grid with time and value labels, the evaluated curve,
// keys, and the tangent handles of the selected key.
class CurveEditorRenderer {
public:
#if defined(_WIN32)
    static void Paint(HDC dc, const RECT& rect, const CurveEditorState& state, const EditorTheme& theme);
#endif
    // Grid spacing of 1, 2 or 5 times a power of ten that gives about `targetLines` lines.
    [[nodiscard]] static float GridStep(float span, float targetLines) noexcept;
};

} // namespace kb::editor
