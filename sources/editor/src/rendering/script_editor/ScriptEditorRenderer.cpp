#include "rendering/script_editor/ScriptEditorRenderer.hpp"

#if defined(_WIN32)
#include "rendering/script_editor/LuaSyntaxHighlighter.hpp"
#include "rendering/script_editor/ScriptEditorDocument.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"
#include "rendering/script_editor/ScriptEditorTheme.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

using namespace script_editor_theme;

[[nodiscard]] HFONT MonospaceFont() {
    static HFONT font = CreateFontW(
        -kFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    return font;
}

void FillColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

} // namespace

bool ScriptEditorRenderer::PaintTo(
    HDC target,
    const RECT& bounds,
    const ScriptEditorDocument& document,
    const ScriptEditorViewport& viewport,
    ScriptEditorMetrics& metrics,
    bool caretVisible,
    bool focused,
    HWND interactionWindow) {
    if (target == nullptr) {
        return false;
    }
    const int width = static_cast<int>(
        std::max(0L, bounds.right - bounds.left));
    const int height = static_cast<int>(
        std::max(0L, bounds.bottom - bounds.top));
    if (width == 0 || height == 0) {
        return false;
    }
    const RECT client{ 0, 0, width, height };
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    if (dc == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (dc != nullptr) {
            DeleteDC(dc);
        }
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HGDIOBJ oldFont = SelectObject(dc, MonospaceFont());
    SetBkMode(dc, TRANSPARENT);

    SIZE measure{};
    GetTextExtentPoint32W(dc, L"0", 1, &measure);
    metrics.charWidth = std::max(1L, measure.cx);

    FillColor(dc, client, kBackground);

    const int gutter = ScriptEditorLayout::GutterWidth(document, metrics);
    const int textLeft = ScriptEditorLayout::TextLeft(document, metrics);
    FillColor(dc, RECT{ 0, 0, gutter, height }, kGutterBackground);

    const ScriptCaret caret = document.Caret();
    const int visible = ScriptEditorLayout::VisibleLineCount(client, metrics);
    const int firstLine = viewport.scrollLine;
    const int lastLine = std::min(document.LineCount(), firstLine + visible + 1);

    ScriptCaret selStart;
    ScriptCaret selEnd;
    document.OrderedSelection(selStart, selEnd);
    const bool selection = document.HasSelection();

    for (int line = firstLine; line < lastLine; ++line) {
        const int y = (line - firstLine) * metrics.lineHeight;
        const std::string& text = document.Lines()[static_cast<std::size_t>(line)];

        if (line == caret.line) {
            FillColor(dc, RECT{ gutter, y, width - kScrollbarWidth, y + metrics.lineHeight }, kActiveLine);
        }

        if (selection && line >= selStart.line && line <= selEnd.line) {
            const int startCol = (line == selStart.line) ? selStart.column : 0;
            const int endCol = (line == selEnd.line) ? selEnd.column : static_cast<int>(text.size()) + 1;
            const int x0 = textLeft - viewport.scrollX + startCol * metrics.charWidth;
            const int x1 = textLeft - viewport.scrollX + endCol * metrics.charWidth;
            RECT selRect{ std::max(gutter, x0), y, std::min(width - kScrollbarWidth, x1), y + metrics.lineHeight };
            if (selRect.right > selRect.left) {
                FillColor(dc, selRect, kSelection);
            }
        }

        const std::wstring number = std::to_wstring(line + 1);
        SetTextColor(dc, line == caret.line ? kGutterActive : kGutterText);
        RECT numberRect{ 0, y, gutter - kGutterPadRight, y + metrics.lineHeight };
        DrawTextW(dc, number.c_str(), -1, &numberRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        const int textTop = y + (metrics.lineHeight - metrics.fontHeight) / 2;
        const HRGN clip = CreateRectRgn(gutter, y, width - kScrollbarWidth, y + metrics.lineHeight);
        SelectClipRgn(dc, clip);
        for (const ScriptToken& token : LuaSyntaxHighlighter::Tokenize(text)) {
            SetTextColor(dc, ScriptTokenColor(token.kind));
            const std::wstring segment = ScriptEditorTextEncoding::Widen(std::string_view{ text.data() + token.start, static_cast<std::size_t>(token.length) });
            const int x = textLeft - viewport.scrollX + token.start * metrics.charWidth;
            TextOutW(dc, x, textTop, segment.c_str(), static_cast<int>(segment.size()));
        }
        SelectClipRgn(dc, nullptr);
        DeleteObject(clip);

        if (line == caret.line && caretVisible && focused) {
            const int caretX = textLeft - viewport.scrollX + caret.column * metrics.charWidth;
            FillColor(dc, RECT{ caretX, y + 2, caretX + 2, y + metrics.lineHeight - 2 }, kCaret);
        }
    }

    const RECT thumb = ScriptEditorLayout::ScrollbarThumb(document, viewport, client, metrics);
    if (thumb.bottom > thumb.top) {
        POINT cursor{};
        const bool hot =
            interactionWindow != nullptr &&
            GetCursorPos(&cursor) != 0 &&
            ScreenToClient(interactionWindow, &cursor) != 0 &&
            cursor.x >= thumb.left && cursor.x < thumb.right &&
            cursor.y >= thumb.top && cursor.y < thumb.bottom;
        FillColor(dc, RECT{ thumb.left + 3, thumb.top + 2, thumb.right - 3, thumb.bottom - 2 }, hot ? kScrollThumbHot : kScrollThumb);
    }

    const bool copied = BitBlt(
        target, bounds.left, bounds.top, width, height,
        dc, 0, 0, SRCCOPY) != 0;
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return copied;
}

void ScriptEditorRenderer::Paint(HWND window, const ScriptEditorDocument& document, const ScriptEditorViewport& viewport, ScriptEditorMetrics& metrics, bool caretVisible, bool focused) {
    RECT client{};
    GetClientRect(window, &client);
    PAINTSTRUCT paint{};
    HDC windowDc = BeginPaint(window, &paint);
    static_cast<void>(PaintTo(
        windowDc, client, document, viewport, metrics,
        caretVisible, focused, window));
    EndPaint(window, &paint);
}

} // namespace kb::editor

#endif
