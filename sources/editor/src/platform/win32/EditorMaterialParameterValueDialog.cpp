#include "platform/win32/EditorMaterialParameterValueDialog.hpp"

#if defined(_WIN32)

#include "platform/win32/EditorModalMessageLoop.hpp"
#include "rendering/script_editor/ScriptEditorTextEncoding.hpp"
#include "platform/win32/EditorModalWindowScope.hpp"

#include <array>

namespace kb::editor {
namespace {

constexpr wchar_t kDialogClassName[] = L"KBEditorMaterialParameterValueDialog";
constexpr UINT_PTR kEditId = 1001U;
constexpr UINT_PTR kOkId = IDOK;
constexpr UINT_PTR kCancelId = IDCANCEL;

struct DialogState {
    HWND window = nullptr;
    HWND edit = nullptr;
    bool done = false;
    std::optional<std::string> result;
    std::string parameterName;
    std::string currentValue;
};

// UTF-8, via the editor's shared conversion. The hand-rolled pair that used to live here was Latin-1 one
// way and ASCII the other: a parameter name outside ASCII rendered as mojibake, and - the part that mattered
// - any non-ASCII character the user typed came back as '?', silently corrupting the value on the way out.
[[nodiscard]] std::wstring Widen(std::string_view text) {
    return ScriptEditorTextEncoding::Widen(text);
}

[[nodiscard]] std::string Narrow(std::wstring_view text) {
    return ScriptEditorTextEncoding::Narrow(text);
}

void Finish(DialogState& state, bool accepted) {
    if (accepted && state.edit != nullptr) {
        const int length = GetWindowTextLengthW(state.edit);
        std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
        if (length > 0) {
            GetWindowTextW(state.edit, value.data(), length + 1);
        }
        value.resize(static_cast<std::size_t>(std::max(0, length)));
        state.result = Narrow(value);
    }
    state.done = true;
    DestroyWindow(state.window);
}

LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = reinterpret_cast<DialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
        return TRUE;
    }
    case WM_CREATE: {
        if (state == nullptr) {
            return -1;
        }
        const std::wstring label = L"Value for " + Widen(state->parameterName) + L":";
        CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
            14, 14, 340, 20, window, nullptr, nullptr, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Widen(state->currentValue).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            14, 42, 340, 24, window, reinterpret_cast<HMENU>(kEditId), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"Use numbers like: 0.25 or 1 0 0 1", WS_CHILD | WS_VISIBLE,
            14, 72, 340, 20, window, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            174, 104, 86, 28, window, reinterpret_cast<HMENU>(kOkId), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            268, 104, 86, 28, window, reinterpret_cast<HMENU>(kCancelId), nullptr, nullptr);
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND:
        // WM_NCDESTROY drops the state pointer, so a control notification that arrives after the window
        // started dying must do nothing rather than write through a stale one. WM_CLOSE below already did.
        if (state == nullptr) {
            break;
        }
        if (LOWORD(wparam) == kOkId) {
            Finish(*state, true);
            return 0;
        }
        if (LOWORD(wparam) == kCancelId) {
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
        // Last message this window ever gets - including when it is destroyed from outside, because its
        // owner went away. Drop both directions of the link here so nothing afterwards can reach the state
        // (which lives on Show's stack) and Show cannot address a handle Windows may already have recycled.
        if (state != nullptr) {
            state->window = nullptr;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void RegisterDialogClass() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kDialogClassName;
    RegisterClassW(&wc);
    registered = true;
}

} // namespace

std::optional<std::string> EditorMaterialParameterValueDialog::Show(
    HWND owner,
    std::string_view parameterName,
    std::string_view currentValue) {
    RegisterDialogClass();
    DialogState state{
        .parameterName = std::string{ parameterName },
        .currentValue = std::string{ currentValue },
    };

    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kDialogClassName,
        L"Edit Value",
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

    // When the loop ends without the dialog closing itself - the app is quitting, or the queue broke - the
    // window is still alive while `state` is one return away from dying. `state.window` is nulled by
    // WM_NCDESTROY, so this only ever touches a window that is genuinely still ours.
    if (state.window != nullptr) {
        DestroyWindow(state.window); // WM_NCDESTROY runs inside this call and nulls state.window
    }

    if (owner != nullptr && IsWindow(owner) != 0) {
        SetForegroundWindow(owner);
    }
    // Belt and braces: only Finish() writes the result and it sets `done` in the same call, so a non-Completed
    // exit already implies an empty result. Stated explicitly so a future exit path cannot turn an app
    // shutdown into a value the user never confirmed.
    if (exit != EditorModalLoopExit::Completed) {
        return std::nullopt;
    }
    return state.result;
}

} // namespace kb::editor

#endif
