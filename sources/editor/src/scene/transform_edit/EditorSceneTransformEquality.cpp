#include "scene/transform_edit/EditorSceneTransformEquality.hpp"

namespace kb::editor {

bool EditorSceneTransformEquality::Same(kb::scene::Vec3 lhs, kb::scene::Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

bool EditorSceneTransformEquality::Same(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

bool EditorSceneTransformEquality::Same(const kb::scene::TransformComponent& lhs, const kb::scene::TransformComponent& rhs) noexcept {
    return Same(lhs.localPosition, rhs.localPosition) &&
        Same(lhs.localRotation, rhs.localRotation) &&
        Same(lhs.localScale, rhs.localScale);
}

} // namespace kb::editor
