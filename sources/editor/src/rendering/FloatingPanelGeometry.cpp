#include "rendering/FloatingPanelGeometry.hpp"

#if defined(_WIN32)
#include "docking/DockGeometry.hpp"

namespace kb::editor {

RECT FloatingPanelGeometry::Content(const RECT& client, int tabStripHeight) noexcept {
    const DockRect content = DockGeometry::PanelContent(
        DockRect{
            .x = static_cast<int>(client.left),
            .y = static_cast<int>(client.top),
            .width = static_cast<int>(client.right - client.left),
            .height = static_cast<int>(client.bottom - client.top),
        },
        tabStripHeight);
    return RECT{
        .left = content.x,
        .top = content.y,
        .right = content.x + content.width,
        .bottom = content.y + content.height,
    };
}

} // namespace kb::editor

#endif
