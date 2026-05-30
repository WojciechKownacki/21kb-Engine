#pragma once

#include "windowing/FloatingWindowControlKind.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class FloatingWindowControlActions {
public:
#if defined(_WIN32)
    [[nodiscard]] bool Execute(HWND window, FloatingWindowControlKind control) const;
#endif
};

} // namespace kb::editor
