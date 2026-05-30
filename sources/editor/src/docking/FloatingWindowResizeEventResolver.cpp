#include "docking/FloatingWindowResizeEventResolver.hpp"

#if defined(_WIN32)

namespace kb::editor {

std::optional<FloatingWindowResizeEvent> FloatingWindowResizeEventResolver::Resolve(const FloatingWindowRegistry& registry, HWND window, int width, int height) noexcept {
    const std::uint32_t panelId = registry.PanelId(window);
    if (panelId == 0) {
        return std::nullopt;
    }
    return FloatingWindowResizeEvent{ .panelId = panelId, .width = width, .height = height };
}

} // namespace kb::editor

#endif
