#include "rendering/EditorSceneViewportGeometry.hpp"

#if defined(_WIN32)

#include <algorithm>
#include <cmath>

namespace kb::editor {

std::uint32_t EditorSceneViewportGeometry::RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

std::uint32_t EditorSceneViewportGeometry::RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

RECT EditorSceneViewportGeometry::CenteredRectFor(const RECT& bounds, std::uint32_t renderWidth, std::uint32_t renderHeight, EditorViewportFitMode fitMode) noexcept {
    const std::uint32_t boundsWidth = RectWidth(bounds);
    const std::uint32_t boundsHeight = RectHeight(bounds);
    if (boundsWidth == 0U || boundsHeight == 0U || renderWidth == 0U || renderHeight == 0U) {
        return bounds;
    }

    double scale = 1.0;
    const double scaleX = static_cast<double>(boundsWidth) / static_cast<double>(renderWidth);
    const double scaleY = static_cast<double>(boundsHeight) / static_cast<double>(renderHeight);
    switch (fitMode) {
    case EditorViewportFitMode::Fit:
        scale = std::min(scaleX, scaleY);
        break;
    case EditorViewportFitMode::OneToOne:
        scale = 1.0;
        break;
    case EditorViewportFitMode::Fill:
        scale = std::max(scaleX, scaleY);
        break;
    }

    const LONG width = std::max<LONG>(1, static_cast<LONG>(std::lround(static_cast<double>(renderWidth) * scale)));
    const LONG height = std::max<LONG>(1, static_cast<LONG>(std::lround(static_cast<double>(renderHeight) * scale)));
    const LONG centerX = bounds.left + static_cast<LONG>(boundsWidth / 2U);
    const LONG centerY = bounds.top + static_cast<LONG>(boundsHeight / 2U);
    return RECT{
        .left = centerX - width / 2,
        .top = centerY - height / 2,
        .right = centerX - width / 2 + width,
        .bottom = centerY - height / 2 + height,
    };
}

RECT EditorSceneViewportGeometry::ClipRectToClient(HWND parent, const RECT& rect) noexcept {
    if (parent == nullptr) {
        return {};
    }
    RECT client{};
    if (GetClientRect(parent, &client) == 0) {
        return {};
    }
    RECT clipped{};
    if (IntersectRect(&clipped, &rect, &client) == 0) {
        return {};
    }
    return clipped;
}

} // namespace kb::editor

#endif
