#include "app/scene_viewport/EditorSceneViewportTypes.hpp"

#if defined(_WIN32)
#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>

namespace kb::editor {

bool EditorSceneViewportMath::Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

float EditorSceneViewportMath::RectWidth(const RECT& rect) noexcept {
    return static_cast<float>(std::max<LONG>(1, rect.right - rect.left));
}

float EditorSceneViewportMath::RectHeight(const RECT& rect) noexcept {
    return static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top));
}

// LIB-044: delegates to the single canonical kb::math::ToRadians instead
// of an independently-rederived degrees-to-radians constant.
float EditorSceneViewportMath::DegreesToRadians(float degrees) noexcept {
    return kb::math::ToRadians(kb::math::Degrees{ degrees }).Value();
}

kb::scene::Vec3 EditorSceneViewportMath::Add(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

kb::scene::Vec3 EditorSceneViewportMath::Sub(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return kb::scene::Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

kb::scene::Vec3 EditorSceneViewportMath::Mul(kb::scene::Vec3 value, float scale) noexcept {
    return kb::scene::Vec3{value.x * scale, value.y * scale, value.z * scale};
}

float EditorSceneViewportMath::Dot(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float EditorSceneViewportMath::LengthSquared(kb::scene::Vec3 value) noexcept {
    return Dot(value, value);
}

kb::scene::Vec3 EditorSceneViewportMath::Normalize(kb::scene::Vec3 value) noexcept {
    const float lengthSquared = LengthSquared(value);
    if (lengthSquared <= 0.000001F) {
        return kb::scene::Vec3{};
    }
    return Mul(value, 1.0F / std::sqrt(lengthSquared));
}

kb::scene::Vec3 EditorSceneViewportMath::AxisWorldDirection(int axis) noexcept {
    switch (axis) {
    case 0: return kb::scene::Vec3{1.0F, 0.0F, 0.0F};
    case 1: return kb::scene::Vec3{0.0F, 1.0F, 0.0F};
    case 2: return kb::scene::Vec3{0.0F, 0.0F, 1.0F};
    default: return {};
    }
}

bool EditorSceneViewportMath::WorldToScreen(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 position,
    float& screenX,
    float& screenY) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    const kb::scene::Vec3 toPoint = Sub(position, axes.position);
    const float viewX = Dot(toPoint, axes.right);
    const float viewY = Dot(toPoint, axes.up);
    const float viewZ = Dot(toPoint, axes.forward);
    if (viewZ <= 0.001F) {
        return false;
    }

    const float width = RectWidth(renderArea);
    const float height = RectHeight(renderArea);
    const float aspect = width / height;
    const float tanHalfFov = std::tan(DegreesToRadians(camera.VerticalFovDegrees()) * 0.5F);
    const float ndcX = viewX / (viewZ * tanHalfFov * aspect);
    const float ndcY = viewY / (viewZ * tanHalfFov);
    screenX = (ndcX * 0.5F + 0.5F) * width;
    screenY = (1.0F - (ndcY * 0.5F + 0.5F)) * height;
    return true;
}

void EditorSceneViewportMath::MoveEntityTo(kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::Vec3 position) {
    kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
    transform.localPosition = position;
    scene.Transforms().Set(entity, transform);
}

} // namespace kb::editor

#endif
