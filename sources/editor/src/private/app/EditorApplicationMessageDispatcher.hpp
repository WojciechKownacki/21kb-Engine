#pragma once

#include "app/EditorApplicationState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorApplicationMessageDispatcher {
public:
    EditorApplicationMessageDispatcher() = delete;

#if defined(_WIN32)
    [[nodiscard]] static LRESULT Dispatch(EditorApplicationState& state, HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam);
#endif
};

} // namespace kb::editor
