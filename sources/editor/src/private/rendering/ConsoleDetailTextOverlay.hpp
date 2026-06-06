#pragma once

#include "console/EditorConsoleState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class ConsoleDetailTextOverlay {
public:
#if defined(_WIN32)
    static void Sync(HWND parent, const RECT& consoleContent, const EditorConsoleState& console);
    static void Hide(HWND parent) noexcept;
#endif
};

} // namespace kb::editor
