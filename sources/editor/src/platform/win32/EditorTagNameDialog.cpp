#include "platform/win32/EditorTagNameDialog.hpp"

#if defined(_WIN32)

#include "platform/win32/EditorModalMessageLoop.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr wchar_t kWindowClass[] = L"KBEditorTagNameDialog";
constexpr UINT_PTR kEditId = 1001U;

struct DialogState {
    HWND window = nullptr;
    HWND edit = nullptr;
    bool done = false;
    std::optional<std::string> result;
};

void Finish(DialogState& state, bool accepted) {
    if (accepted && state.edit != nullptr) {
        const int length = GetWindowTextLengthW(state.edit);
        std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
        if (length > 0) {
            GetWindowTextW(state.edit, value.data(), length + 1);
        }
        value.resize(static_cast<std::size_t>(std::max(0, length)));
        state.result = ScriptEditorTextEncoding::Narrow(value);
    }
    state.done = true;
    DestroyWindow(state.window);
}

LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
        return TRUE;
    }
    case WM_CREATE:
        if (state == nullptr) {
            return -1;
        }
        CreateWindowExW(0, L"STATIC", L"Tag name:", WS_CHILD | WS_VISIBLE, 14, 14, 340, 20, window, nullptr, nullptr, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            14, 42, 340, 24, window, reinterpret_cast<HMENU>(kEditId), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"Names cannot contain commas or semicolons.", WS_CHILD | WS_VISIBLE,
            14, 72, 340, 20, window, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            174, 104, 86, 28, window, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            268, 104, 86, 28, window, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SetFocus(state->edit);
        return 0;
    case WM_COMMAND:
        if (state != nullptr && LOWORD(wparam) == IDOK) {
            Finish(*state, true);
            return 0;
        }
        if (state != nullptr && LOWORD(wparam) == IDCANCEL) {
            Finish(*state, false);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state != nullptr) {
            Finish(*state, false);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        if (state != nullptr) {
            state->window = nullptr;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void RegisterWindowClass() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = DialogProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    static_cast<void>(RegisterClassW(&windowClass));
    registered = true;
}

} // namespace

std::optional<std::string> EditorTagNameDialog::Show(HWND owner) {
    RegisterWindowClass();
    DialogState state;
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWindowClass,
        L"New Tag",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        384,
        180,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (window == nullptr) {
        return std::nullopt;
    }
    EditorModalLoopExit exit = EditorModalLoopExit::Completed;
    {
        const EditorModalWindowScope modal{ window };
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        exit = RunEditorModalMessageLoop(window, true, [&state]() noexcept { return state.done; });
    }
    if (state.window != nullptr) {
        DestroyWindow(state.window);
    }
    if (owner != nullptr && IsWindow(owner) != 0) {
        SetForegroundWindow(owner);
    }
    return exit == EditorModalLoopExit::Completed ? state.result : std::nullopt;
}

} // namespace kb::editor

#endif
