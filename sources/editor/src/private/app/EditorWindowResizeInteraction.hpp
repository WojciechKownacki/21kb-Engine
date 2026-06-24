#pragma once

#include "docking/EditorDockController.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowResizeInteraction {
public:
    EditorWindowResizeInteraction() = delete;

#if defined(_WIN32)
    static void BeginWindowResize(HWND window) noexcept;
    static void EndWindowResize(HWND window) noexcept;
    [[nodiscard]] static bool IsWindowResizing(HWND window) noexcept;
    [[nodiscard]] static bool IsInteractiveResize(HWND window, const EditorDockController& dockController) noexcept;
#endif
};

} // namespace kb::editor
