#include "app/EditorApplicationWindowProc.hpp"

#if defined(_WIN32)
#include "app/EditorApplicationMessageDispatcher.hpp"
#include "app/EditorApplicationState.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] EditorApplicationState* ResolveState(HWND window, UINT message, LPARAM lparam) noexcept {
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* state = static_cast<EditorApplicationState*>(createStruct->lpCreateParams);
        if (state != nullptr && state->window == nullptr) {
            state->window = window;
        }
        if (state != nullptr) {
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        return state;
    }

    return reinterpret_cast<EditorApplicationState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

} // namespace

LRESULT CALLBACK EditorApplicationWindowProc::Handle(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    EditorApplicationState* state = ResolveState(window, message, lparam);
    if (state != nullptr) {
        if (state->inputCollector.HandleWindowMessage(window, message, wparam, lparam)) {
            return 0;
        }
        return EditorApplicationMessageDispatcher::Dispatch(*state, window, message, wparam, lparam);
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace kb::editor

#endif
