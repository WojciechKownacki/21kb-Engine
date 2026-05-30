#include "docking/FloatingWindowDestroyer.hpp"

#if defined(_WIN32)
#include <vector>

namespace kb::editor {

void FloatingWindowDestroyer::DestroyPanel(FloatingWindowRegistry& registry, std::uint32_t panelId) {
    HWND floating = registry.WindowForPanel(panelId);
    registry.RemovePanel(panelId);
    if (floating != nullptr) {
        DestroyWindow(floating);
    }
}

void FloatingWindowDestroyer::DestroyAll(FloatingWindowRegistry& registry) {
    const std::vector<HWND> windows = registry.Windows();
    registry.Clear();

    for (HWND window : windows) {
        DestroyWindow(window);
    }
}

} // namespace kb::editor

#endif
