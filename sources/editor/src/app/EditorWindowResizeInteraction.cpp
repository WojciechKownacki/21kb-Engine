#include "app/EditorWindowResizeInteraction.hpp"

#if defined(_WIN32)
#include "docking/DockPointerDrag.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

std::vector<HWND>& ActiveResizeWindows() noexcept {
    static std::vector<HWND> windows;
    return windows;
}

} // namespace

void EditorWindowResizeInteraction::BeginWindowResize(HWND window) noexcept {
    if (window == nullptr) {
        return;
    }
    std::vector<HWND>& windows = ActiveResizeWindows();
    if (std::ranges::find(windows, window) == windows.end()) {
        windows.push_back(window);
    }
}

void EditorWindowResizeInteraction::EndWindowResize(HWND window) noexcept {
    std::vector<HWND>& windows = ActiveResizeWindows();
    std::erase(windows, window);
}

bool EditorWindowResizeInteraction::IsWindowResizing(HWND window) noexcept {
    const std::vector<HWND>& windows = ActiveResizeWindows();
    return std::ranges::find(windows, window) != windows.end();
}

bool EditorWindowResizeInteraction::IsInteractiveResize(HWND window, const EditorDockController& dockController) noexcept {
    if (IsWindowResizing(window)) {
        return true;
    }
    const DockPointerDrag* drag = dockController.ActiveDrag();
    return drag != nullptr && drag->kind == DockHitKind::Splitter;
}

} // namespace kb::editor

#endif
