#include "docking/FloatingWindowResizeEventResolver.hpp"

#if defined(_WIN32)

namespace kb::editor {

std::optional<FloatingWindowResizeEvent> FloatingWindowResizeEventResolver::Resolve(std::uint32_t panelId, int width, int height) noexcept {
    if (panelId == 0) {
        return std::nullopt;
    }
    return FloatingWindowResizeEvent{ .panelId = panelId, .width = width, .height = height };
}

} // namespace kb::editor

#endif
