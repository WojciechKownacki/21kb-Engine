#pragma once
#include "engine/scene/SceneUI.hpp"
#include <vector>
namespace kb::editor {
struct EditorUIRectDragState {
    kb::scene::SceneEntity entity{};
    kb::scene::UIRectTransform original{};
    kb::math::Vec2 pointerStart{};
    kb::math::Vec2 viewportOrigin{};
    float pointerScale = 1.0F;
    kb::math::Vec2 basisX{}, basisY{};
    kb::math::Vec2 size{};
    int handle = -1;
    bool changed = false;
    // Snapping: the widget's rectangle on the frame when it was grabbed, the lines of the other widgets and of
    // the canvas it may snap to (empty when the widget is rotated), and the lines it snapped to last, which
    // the viewport draws as guides.
    kb::math::Rect grabbed{};
    float snapDistance = 0.0F;
    kb::math::Vec2 extent{};
    std::vector<float> snapX, snapY, guideX, guideY;
};
} // namespace kb::editor
