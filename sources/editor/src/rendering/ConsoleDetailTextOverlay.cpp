#include "rendering/ConsoleDetailTextOverlay.hpp"

#if defined(_WIN32)
#include "console/EditorConsoleLayout.hpp"

#include <string>
#include <unordered_map>

namespace kb::editor {
namespace {

struct ConsoleDetailEditControl {
    HWND edit = nullptr;
    std::uint64_t sequence = 0;
    RECT bounds{};
};

[[nodiscard]] std::unordered_map<HWND, ConsoleDetailEditControl>& Controls() {
    static std::unordered_map<HWND, ConsoleDetailEditControl> controls;
    return controls;
}

[[nodiscard]] HFONT DetailFont() {
    static HFONT font = CreateFontW(
        -13,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    return font;
}

[[nodiscard]] std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring{text.begin(), text.end()};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

[[nodiscard]] RECT EditBounds(const RECT& consoleContent, const EditorConsoleState& console) noexcept {
    const EditorConsoleLayoutRects layout = ResolveEditorConsoleLayout(consoleContent, console);
    RECT rect = layout.detailTextArea;
    if (rect.right < rect.left) {
        rect.right = rect.left;
    }
    if (rect.bottom < rect.top) {
        rect.bottom = rect.top;
    }
    return rect;
}

} // namespace

void ConsoleDetailTextOverlay::Sync(HWND parent, const RECT& consoleContent, const EditorConsoleState& console) {
    if (parent == nullptr) {
        return;
    }
    const EditorConsoleEntry* selected = console.SelectedEntry();
    if (selected == nullptr) {
        Hide(parent);
        return;
    }

    ConsoleDetailEditControl& control = Controls()[parent];
    if (control.edit == nullptr || !IsWindow(control.edit)) {
        control.edit = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_WANTRETURN | ES_NOHIDESEL,
            0,
            0,
            0,
            0,
            parent,
            nullptr,
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
            nullptr);
        SendMessageW(control.edit, WM_SETFONT, reinterpret_cast<WPARAM>(DetailFont()), TRUE);
    }

    const RECT bounds = EditBounds(consoleContent, console);
    if (bounds.left != control.bounds.left || bounds.top != control.bounds.top || bounds.right != control.bounds.right || bounds.bottom != control.bounds.bottom) {
        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
        SetWindowPos(control.edit, nullptr, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, flags);
        control.bounds = bounds;
        RedrawWindow(control.edit, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
    if (control.sequence != selected->sequence) {
        const std::wstring text = Utf8ToWide(selected->message);
        SetWindowTextW(control.edit, text.c_str());
        SendMessageW(control.edit, EM_SETSEL, 0, 0);
        control.sequence = selected->sequence;
    }
    const int firstVisible = static_cast<int>(SendMessageW(control.edit, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int delta = console.DetailScrollLine() - firstVisible;
    if (delta != 0) {
        SendMessageW(control.edit, EM_LINESCROLL, 0, delta);
    }
    ShowWindow(control.edit, SW_SHOW);
}

void ConsoleDetailTextOverlay::Hide(HWND parent) noexcept {
    auto it = Controls().find(parent);
    if (it == Controls().end()) {
        return;
    }
    if (it->second.edit != nullptr && IsWindow(it->second.edit)) {
        ShowWindow(it->second.edit, SW_HIDE);
    }
    it->second.sequence = 0;
}

} // namespace kb::editor

#endif
