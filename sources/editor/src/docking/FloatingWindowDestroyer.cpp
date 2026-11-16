#include "docking/FloatingWindowDestroyer.hpp"

#if defined(_WIN32)
#include <algorithm>

namespace kb::editor {

void FloatingWindowDestroyer::DestroyOrphaned(FloatingWindowRegistry& registry, const std::vector<std::uint32_t>& liveGroups) {
    for (const std::uint32_t groupId : registry.Groups()) {
        if (std::ranges::find(liveGroups, groupId) != liveGroups.end()) {
            continue;
        }
        HWND floating = registry.WindowForGroup(groupId);
        registry.RemoveGroup(groupId);
        if (floating != nullptr) {
            DestroyWindow(floating);
        }
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
