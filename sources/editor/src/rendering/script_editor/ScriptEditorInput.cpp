#include "rendering/script_editor/ScriptEditorInput.hpp"

#if defined(_WIN32)
#include "rendering/script_editor/ScriptEditorClipboard.hpp"
#include "rendering/script_editor/ScriptEditorDocument.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] RECT ClientRect(HWND window) noexcept {
    RECT client{};
    GetClientRect(window, &client);
    return client;
}

[[nodiscard]] bool KeyHeld(int virtualKey) noexcept {
    return (GetKeyState(virtualKey) & 0x8000) != 0;
}

ScriptEditorInputResult HandleControlKey(HWND window, ScriptEditorDocument& document, ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, WPARAM key) {
    switch (key) {
    case 'S':
        return ScriptEditorInputResult{ .changed = false, .saveRequested = true };
    case 'A':
        document.SelectAll();
        return ScriptEditorInputResult{ .changed = true };
    case 'C':
        ScriptEditorClipboard::Set(window, [&document] {
            ScriptCaret start;
            ScriptCaret end;
            document.OrderedSelection(start, end);
            std::string text;
            for (int line = start.line; line <= end.line; ++line) {
                const std::string& current = document.Lines()[static_cast<std::size_t>(line)];
                const int from = (line == start.line) ? start.column : 0;
                const int to = (line == end.line) ? end.column : static_cast<int>(current.size());
                text += current.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
                if (line < end.line) {
                    text.push_back('\n');
                }
            }
            return text;
        }());
        return ScriptEditorInputResult{ .changed = false };
    case 'X': {
        if (document.HasSelection()) {
            ScriptCaret start;
            ScriptCaret end;
            document.OrderedSelection(start, end);
            std::string text;
            for (int line = start.line; line <= end.line; ++line) {
                const std::string& current = document.Lines()[static_cast<std::size_t>(line)];
                const int from = (line == start.line) ? start.column : 0;
                const int to = (line == end.line) ? end.column : static_cast<int>(current.size());
                text += current.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
                if (line < end.line) {
                    text.push_back('\n');
                }
            }
            ScriptEditorClipboard::Set(window, text);
            document.PushUndo();
            document.DeleteSelection();
            ScriptEditorLayout::EnsureCaretVisible(document, viewport, ClientRect(window), metrics);
            return ScriptEditorInputResult{ .changed = true };
        }
        return ScriptEditorInputResult{ .changed = false };
    }
    case 'V': {
        const std::string pasted = ScriptEditorClipboard::Get(window);
        if (!pasted.empty()) {
            document.PushUndo();
            document.InsertText(pasted);
            ScriptEditorLayout::EnsureCaretVisible(document, viewport, ClientRect(window), metrics);
            return ScriptEditorInputResult{ .changed = true };
        }
        return ScriptEditorInputResult{ .changed = false };
    }
    case 'Z':
        if (document.Undo()) {
            ScriptEditorLayout::EnsureCaretVisible(document, viewport, ClientRect(window), metrics);
            return ScriptEditorInputResult{ .changed = true };
        }
        return ScriptEditorInputResult{ .changed = false };
    case 'Y':
        if (document.Redo()) {
            ScriptEditorLayout::EnsureCaretVisible(document, viewport, ClientRect(window), metrics);
            return ScriptEditorInputResult{ .changed = true };
        }
        return ScriptEditorInputResult{ .changed = false };
    default:
        return ScriptEditorInputResult{ .changed = false };
    }
}

} // namespace

ScriptEditorInputResult ScriptEditorInput::HandleKeyDown(HWND window, ScriptEditorDocument& document, ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, WPARAM key) {
    const RECT client = ClientRect(window);
    const bool shift = KeyHeld(VK_SHIFT);
    if (KeyHeld(VK_CONTROL)) {
        return HandleControlKey(window, document, viewport, metrics, key);
    }

    switch (key) {
    case VK_LEFT:
        document.MoveHorizontal(-1, shift);
        break;
    case VK_RIGHT:
        document.MoveHorizontal(1, shift);
        break;
    case VK_UP:
        document.MoveVertical(-1, shift);
        break;
    case VK_DOWN:
        document.MoveVertical(1, shift);
        break;
    case VK_PRIOR:
        document.MoveVertical(-ScriptEditorLayout::VisibleLineCount(client, metrics), shift);
        break;
    case VK_NEXT:
        document.MoveVertical(ScriptEditorLayout::VisibleLineCount(client, metrics), shift);
        break;
    case VK_HOME:
        document.MoveLineStart(shift);
        break;
    case VK_END:
        document.MoveLineEnd(shift);
        break;
    case VK_RETURN:
        document.PushUndo();
        document.InsertText("\n");
        break;
    case VK_BACK:
        document.PushUndo();
        document.DeleteBackward();
        break;
    case VK_DELETE:
        document.PushUndo();
        document.DeleteForward();
        break;
    case VK_TAB:
        document.PushUndo();
        document.InsertText("    ");
        break;
    default:
        return ScriptEditorInputResult{ .changed = false };
    }
    ScriptEditorLayout::EnsureCaretVisible(document, viewport, client, metrics);
    return ScriptEditorInputResult{ .changed = true };
}

bool ScriptEditorInput::HandleChar(HWND window, ScriptEditorDocument& document, ScriptEditorViewport& viewport, const ScriptEditorMetrics& metrics, wchar_t character) {
    if (character < 32 || character == 127) {
        return false;
    }
    document.PushUndo();
    document.InsertText(ScriptEditorTextEncoding::Narrow(std::wstring_view{ &character, 1 }));
    ScriptEditorLayout::EnsureCaretVisible(document, viewport, ClientRect(window), metrics);
    return true;
}

} // namespace kb::editor

#endif
