#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "scene/EditorViewportCameraState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

#if defined(_WIN32)

// In viewport space (EditorViewportCameraState): relative to the camera's viewport origin.
struct EditorSceneViewportRay {
    kb::scene::Vec3 origin{};
    kb::scene::Vec3 direction{};
};

struct EditorSceneViewportHit {
    std::uint32_t panelId = 0;
    RECT renderArea{};
    EditorSceneViewportRay ray{};
    kb::scene::Vec3 groundPosition{};
    float localX = 0.0F;
    float localY = 0.0F;
    // The world position the ray and the ground position are relative to (the camera's viewport origin).
    kb::math::DVec3 origin{};
};

class EditorSceneViewportMath {
public:
    EditorSceneViewportMath() = delete;

    [[nodiscard]] static bool Contains(const RECT& rect, int x, int y) noexcept;
    [[nodiscard]] static float RectWidth(const RECT& rect) noexcept;
    [[nodiscard]] static float RectHeight(const RECT& rect) noexcept;
    [[nodiscard]] static float DegreesToRadians(float degrees) noexcept;
    [[nodiscard]] static kb::scene::Vec3 Add(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept;
    [[nodiscard]] static kb::scene::Vec3 Sub(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept;
    [[nodiscard]] static kb::scene::Vec3 Mul(kb::scene::Vec3 value, float scale) noexcept;
    [[nodiscard]] static float Dot(kb::scene::Vec3 a, kb::scene::Vec3 b) noexcept;
    [[nodiscard]] static float LengthSquared(kb::scene::Vec3 value) noexcept;
    [[nodiscard]] static kb::scene::Vec3 Normalize(kb::scene::Vec3 value) noexcept;
    [[nodiscard]] static kb::scene::Vec3 AxisWorldDirection(int axis) noexcept;
    [[nodiscard]] static bool WorldToScreen(
        const EditorViewportCameraState& camera,
        const RECT& renderArea,
        kb::scene::Vec3 position,
        float& screenX,
        float& screenY) noexcept;
    static void MoveEntityTo(kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::Vec3 position);
    // Places the entity at a world position given in double precision.
    static void MoveEntityTo(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::math::DVec3& position);
    // The entity's world translation in viewport space (relative to `origin`).
    [[nodiscard]] static kb::scene::Vec3 ViewportPosition(
        const kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::math::DVec3& origin) noexcept;
};

#endif

} // namespace kb::editor
