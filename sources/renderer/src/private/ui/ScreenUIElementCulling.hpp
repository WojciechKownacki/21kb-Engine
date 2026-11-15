#pragma once

#include "engine/scene/SceneUI.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {

// Whether anything of the element can land inside the rectangle that clips it. An element laid out far outside a scroll
// view's window, or off the screen, is neither shaped nor drawn. The margin covers what is drawn beyond the element's own
// corners: a shadow, an outline, and text that runs past the box it is laid into - which is bounded by what the string
// can occupy at its size, one and a half em per character on a line.
[[nodiscard]] inline bool ScreenUIElementMayShow(const kb::scene::SceneUIFrameElement& element) noexcept {
    float left = element.corners[0].x;
    float right = left;
    float top = element.corners[0].y;
    float bottom = top;
    for (const kb::math::Vec2& corner : element.corners) {
        left = std::min(left, corner.x);
        right = std::max(right, corner.x);
        top = std::min(top, corner.y);
        bottom = std::max(bottom, corner.y);
    }
    const float scale = std::max(element.canvasScale, 0.0F);
    float margin = 0.0F;
    if (element.shadow.has_value()) {
        const kb::scene::UIShadow& shadow = *element.shadow;
        margin = std::max(margin, (shadow.blur + std::max(std::abs(shadow.offset.x), std::abs(shadow.offset.y))) * scale);
    }
    if (element.outline.has_value()) {
        margin = std::max(margin, element.outline->width * scale);
    }
    if (element.text.has_value()) {
        const kb::scene::UIText& text = *element.text;
        const float advance = (text.fontSize * 1.5F + std::max(text.characterSpacing, 0.0F)) * scale;
        margin = std::max(margin, static_cast<float>(kb::scene::UITextContent(text).size()) * advance);
    }
    const kb::math::Rect& clip = element.clipRect;
    return !(right + margin < clip.x || left - margin > clip.x + clip.width || bottom + margin < clip.y ||
             top - margin > clip.y + clip.height);
}

} // namespace kb::render
