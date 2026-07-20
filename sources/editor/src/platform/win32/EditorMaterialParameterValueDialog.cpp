#include "platform/win32/EditorMaterialParameterValueDialog.hpp"

#if defined(_WIN32)

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

[[nodiscard]] std::wstring Widen(std::string_view text) {
    std::wstring wide;
    wide.reserve(text.size());
    for (const unsigned char ch : text) {
        wide.push_back(static_cast<wchar_t>(ch));
    }
    return wide;
}

[[nodiscard]] std::string Narrow(std::wstring_view text) {
    std::string narrow;
    narrow.reserve(text.size());
    for (const wchar_t ch : text) {
        narrow.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
    }
    return narrow;
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
        L"Edit Material Graph Parameter",
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
    {
        const EditorModalWindowScope modal{ window };
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        MSG msg{};
        while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    if (owner != nullptr && IsWindow(owner) != 0) {
        SetForegroundWindow(owner);
    }
    return state.result;
}

} // namespace kb::editor

#endif
