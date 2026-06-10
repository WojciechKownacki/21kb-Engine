#pragma once

#include "rendering/script_editor/ScriptEditorDocument.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Font metrics (measured at paint time) used to map between text and pixels.
struct ScriptEditorMetrics {
    int charWidth = 8;
    int lineHeight = 19;
    int fontHeight = 16;
};

// Scroll state of the editor view (kept out of the document model).
struct ScriptEditorViewport {
    int scrollLine = 0;
    int scrollX = 0;
};

// Geometry of the editor: gutter width, visible lines, caret hit-testing, the
// scrollbar thumb and the scroll adjustment that keeps the caret on screen. Pure
// math over the document + viewport + metrics; no painting.
class ScriptEditorLayout {
public:
    ScriptEditorLayout() = delete;

    [[nodiscard]] static int GutterWidth(const ScriptEditorDocument& document, const ScriptEditorMetrics& metrics);
    [[nodiscard]] static int TextLeft(const ScriptEditorDocument& document, const ScriptEditorMetrics& metrics);
    [[nodiscard]] static int VisibleLineCount(const RECT& client, const ScriptEditorMetrics& metrics);
    [[nodiscard]] static int MaxScrollLine(const ScriptEditorDocument& document, const RECT& client, const ScriptEditorMetrics& metrics);

    [[nodiscard]] static ScriptCaret CaretFromPoint(const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, int x, int y);
    [[nodiscard]] static RECT ScrollbarThumb(const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, const RECT& client, const ScriptEditorMetrics& metrics);

    static void EnsureCaretVisible(const ScriptEditorDocument& document, ScriptEditorViewport& viewport, const RECT& client, const ScriptEditorMetrics& metrics);
};

#endif

} // namespace kb::editor
