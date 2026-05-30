#pragma once

#include "app/EditorWindowMessageContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowPointerMessageDispatcher {
public:
#if defined(_WIN32)
    explicit EditorWindowPointerMessageDispatcher(EditorWindowMessageContext context) noexcept;

    [[nodiscard]] LRESULT Dispatch(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam) const;
#endif

private:
#if defined(_WIN32)
    EditorWindowMessageContext context_;
#endif
};

} // namespace kb::editor
