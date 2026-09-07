#pragma once
#include "engine/scene/SceneUI.hpp"
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
};
} // namespace kb::editor
