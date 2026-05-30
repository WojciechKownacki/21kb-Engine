#pragma once

#include "app/EditorWindowMessageContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowMessageRouter {
public:
#if defined(_WIN32)
    explicit EditorWindowMessageRouter(EditorWindowMessageContext context) noexcept;

    [[nodiscard]] LRESULT Handle(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam);
#endif

private:
#if defined(_WIN32)
    EditorWindowMessageContext context_;
#endif
};

} // namespace kb::editor
