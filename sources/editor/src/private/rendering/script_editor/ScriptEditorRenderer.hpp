#pragma once

#include "rendering/script_editor/ScriptEditorLayout.hpp"
#include "rendering/script_editor/ScriptProblem.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <vector>

namespace kb::editor {

class ScriptEditorDocument;

#if defined(_WIN32)

// The warnings to mark in the text and list, and the one under the mouse (shown in a tooltip near hoverPoint).
struct ScriptProblemOverlay {
    const std::vector<ScriptProblem>& problems;
    int hovered = -1;
    POINT hoverPoint{};
};

// Paints the editor (double-buffered) for the current document and viewport:
// gutter with line numbers, active-line highlight, selection, syntax-highlighted
// text and the dark scrollbar. Only visible lines are drawn (virtualized), so
// cost is independent of document length. Updates metrics.charWidth from the DC.
class ScriptEditorRenderer {
public:
    ScriptEditorRenderer() = delete;

    static void Paint(HWND window, const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, ScriptEditorMetrics& metrics, const ScriptProblemOverlay& overlay, bool caretVisible, bool focused);
    [[nodiscard]] static bool PaintTo(
        HDC target,
        const RECT& bounds,
        const ScriptEditorDocument& document,
        const ScriptEditorViewport& viewport,
        ScriptEditorMetrics& metrics,
        const ScriptProblemOverlay& overlay,
        bool caretVisible,
        bool focused,
        HWND interactionWindow = nullptr);
};

#endif

} // namespace kb::editor
