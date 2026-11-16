#include "rendering/script_editor/ScriptEditorLayout.hpp"

#include "rendering/script_editor/ScriptEditorTheme.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

using namespace script_editor_theme;

[[nodiscard]] int LineDigits(const ScriptEditorDocument& document) {
    return std::max(2, static_cast<int>(std::to_string(document.LineCount()).size()));
}

} // namespace

int ScriptEditorLayout::GutterWidth(const ScriptEditorDocument& document, const ScriptEditorMetrics& metrics) {
    return LineDigits(document) * metrics.charWidth + kGutterPadRight * 2;
}

int ScriptEditorLayout::TextLeft(const ScriptEditorDocument& document, const ScriptEditorMetrics& metrics) {
    return GutterWidth(document, metrics) + kTextPadLeft;
}

int ScriptEditorLayout::VisibleLineCount(const RECT& client, const ScriptEditorMetrics& metrics) {
    return std::max(1, (static_cast<int>(client.bottom - client.top) - metrics.reservedBottom) / std::max(1, metrics.lineHeight));
}

int ScriptEditorLayout::MaxScrollLine(const ScriptEditorDocument& document, const RECT& client, const ScriptEditorMetrics& metrics) {
    return std::max(0, document.LineCount() - VisibleLineCount(client, metrics));
}

ScriptCaret ScriptEditorLayout::CaretFromPoint(const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, int x, int y) {
    const int textLeft = TextLeft(document, metrics);
    ScriptCaret caret{};
    caret.line = std::clamp(viewport.scrollLine + y / std::max(1, metrics.lineHeight), 0, document.LineCount() - 1);
    const int column = (x - textLeft + viewport.scrollX + metrics.charWidth / 2) / std::max(1, metrics.charWidth);
    caret.column = std::clamp(column, 0, document.LineLength(caret.line));
    return caret;
}

RECT ScriptEditorLayout::ScrollbarThumb(const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, const RECT& client, const ScriptEditorMetrics& metrics) {
    const int trackHeight = static_cast<int>(client.bottom - client.top) - metrics.reservedBottom;
    const int total = document.LineCount();
    const int visible = VisibleLineCount(client, metrics);
    if (total <= visible) {
        return RECT{ 0, 0, 0, 0 };
    }
    const int thumbHeight = std::max(28, trackHeight * visible / total);
    const int maxScroll = std::max(1, total - visible);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int top = travel * std::clamp(viewport.scrollLine, 0, maxScroll) / maxScroll;
    return RECT{ client.right - kScrollbarWidth, top, client.right, top + thumbHeight };
}

int ScriptEditorLayout::ProblemPanelHeight(std::size_t problemCount, const ScriptEditorMetrics& metrics) {
    return problemCount == 0U ? 0 : 1 + std::min(static_cast<int>(problemCount), kMaxProblemRows) * metrics.lineHeight;
}

int ScriptEditorLayout::ShownProblemCount(std::size_t problemCount) {
    return problemCount > static_cast<std::size_t>(kMaxProblemRows) ? kMaxProblemRows - 1 : static_cast<int>(problemCount);
}

int ScriptEditorLayout::ProblemRowAt(std::size_t problemCount, const RECT& client, const ScriptEditorMetrics& metrics, int y) {
    const int top = static_cast<int>(client.bottom - client.top) - ProblemPanelHeight(problemCount, metrics) + 1;
    if (problemCount == 0U || y < top) {
        return -1;
    }
    const int row = (y - top) / std::max(1, metrics.lineHeight);
    return row < ShownProblemCount(problemCount) ? row : -1;
}

int ScriptEditorLayout::ProblemAt(const std::vector<ScriptProblem>& problems, const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, const RECT& client, const ScriptEditorMetrics& metrics, int x, int y) {
    const int textX = x - TextLeft(document, metrics) + viewport.scrollX;
    if (y < 0 || y >= static_cast<int>(client.bottom - client.top) - metrics.reservedBottom || textX < 0) {
        return -1;
    }
    const int line = viewport.scrollLine + y / std::max(1, metrics.lineHeight);
    const int column = textX / std::max(1, metrics.charWidth);
    const auto first = std::ranges::lower_bound(problems, line, {}, &ScriptProblem::line);
    for (auto it = first; it != problems.end() && it->line == line; ++it) {
        if (column >= it->column && column < it->column + it->length) {
            return static_cast<int>(it - problems.begin());
        }
    }
    return -1;
}

void ScriptEditorLayout::EnsureCaretVisible(const ScriptEditorDocument& document, ScriptEditorViewport& viewport, const RECT& client, const ScriptEditorMetrics& metrics) {
    const ScriptCaret caret = document.Caret();
    const int visible = VisibleLineCount(client, metrics);
    viewport.scrollLine = std::clamp(viewport.scrollLine, std::max(0, caret.line - visible + 1), std::max(0, caret.line));

    const int textLeft = TextLeft(document, metrics);
    const int caretX = caret.column * metrics.charWidth;
    const int viewWidth = std::max(1, static_cast<int>(client.right - client.left) - textLeft - kScrollbarWidth);
    if (caretX - viewport.scrollX < 0) {
        viewport.scrollX = caretX;
    } else if (caretX - viewport.scrollX > viewWidth - metrics.charWidth) {
        viewport.scrollX = caretX - viewWidth + metrics.charWidth;
    }
    viewport.scrollX = std::max(0, viewport.scrollX);
}

} // namespace kb::editor
