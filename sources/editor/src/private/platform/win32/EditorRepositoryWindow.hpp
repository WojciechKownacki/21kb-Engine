#pragma once

#include "app/EditorShellInteractionState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// The window that hangs from the top bar's branch label: connect the project to GitHub or to a
// repository at an address, make a local one, or see where it is connected. It stays open beside
// other programs, because signing in to GitHub happens in them, and closes on a click in the editor.
class EditorRepositoryWindow final {
public:
    EditorRepositoryWindow() = delete;

    // Opens the window with its top-left corner at `anchor` (screen coordinates); the click that
    // has just closed it does not open it again.
    static void Toggle(HWND owner, POINT anchor, EditorShellInteractionState& shellInteraction);
};

#endif

} // namespace kb::editor
