#include "scene/transform_edit/EditorSceneTransformMath.hpp"

#include <cmath>

namespace kb::editor {

kb::scene::Quat EditorSceneTransformMath::Normalize(kb::scene::Quat value) noexcept {
    constexpr float kEpsilon = 0.000001F;
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (lengthSquared <= kEpsilon) {
        return kb::scene::Quat{};
    }

    const float invLength = 1.0F / std::sqrt(lengthSquared);
    return kb::scene::Quat{ value.x * invLength, value.y * invLength, value.z * invLength, value.w * invLength };
}

kb::scene::Quat EditorSceneTransformMath::Multiply(kb::scene::Quat lhs, kb::scene::Quat rhs) noexcept {
    return kb::scene::Quat{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

} // namespace kb::editor
