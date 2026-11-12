#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorViewportOverlayPick.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/PhysicsDebugDraw.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"
#include "kb/render/overlay/EditorCameraWireframe.hpp"
#include "kb/render/overlay/EditorLightWireframe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kDefaultPickHalfExtent = 1.0F;
constexpr float kRayAabbParallelEpsilon = 0.00001F;
// Half the side of a drawn Scene View icon in pixels: the icon is 0.30 of the 90-pixel gizmo
// drawn over a 1.16 axis length, and reaches one icon unit with its shadow.
constexpr float kIconHalfSizePixels = 0.30F * 90.0F / 1.16F;

struct ScreenPoint {
    float x = 0.0F;
    float y = 0.0F;
};

// Picking works in viewport space: the ray, the projected positions and the published bounds are relative to
// `origin`, the camera's viewport origin (zero for a ray in world space).
struct NearestPickContext {
    EditorSceneViewportRay ray{};
    EditorSceneViewportPickResult result{};
    const kb::scene::Scene* scene = nullptr;
    kb::math::DVec3 origin{};
    EditorViewportOverlayPicker* overlays = nullptr;
    std::vector<kb::render::EditorLightWireframeLine> lightLines{};
};

struct RectPickContext {
    const kb::scene::Scene* scene = nullptr;
    const EditorViewportCameraState* camera = nullptr;
    kb::math::DVec3 origin{};
    RECT renderArea{};
    RECT selectionRect{};
    std::vector<kb::scene::SceneEntity> entities;
};

[[nodiscard]] kb::scene::Vec3 InverseRotate(kb::scene::Quat rotation, kb::scene::Vec3 value) noexcept {
    const kb::scene::Vec3 q{-rotation.x, -rotation.y, -rotation.z};
    const kb::scene::Vec3 uv{
        q.y * value.z - q.z * value.y,
        q.z * value.x - q.x * value.z,
        q.x * value.y - q.y * value.x,
    };
    const kb::scene::Vec3 uuv{
        q.y * uv.z - q.z * uv.y,
        q.z * uv.x - q.x * uv.z,
        q.x * uv.y - q.y * uv.x,
    };
    return EditorSceneViewportMath::Add(
        value,
        EditorSceneViewportMath::Add(
            EditorSceneViewportMath::Mul(uv, 2.0F * rotation.w),
            EditorSceneViewportMath::Mul(uuv, 2.0F)));
}

// LIB-043: kb::scene::Quat is an alias to kb::math::Quat (TransformComponent.hpp),
// which already provides Normalize and Rotate (this file's own Rotate
// formula assumed a unit-length quaternion, which every caller here
// already passes, so it's numerically the same as kb::math::Rotate's
// general formula) — this file's own copies would now be ambiguous
// overloads via ADL against kb::math's.
using kb::math::Normalize;
using kb::math::Rotate;

[[nodiscard]] kb::scene::Vec3 ResolveViewportPosition(
    const kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::math::DVec3& origin) noexcept {
    return EditorSceneViewportMath::ViewportPosition(scene, entity, transform, origin);
}

[[nodiscard]] kb::scene::Quat ResolveWorldRotation(const kb::scene::TransformComponent& transform) noexcept {
    return Normalize(transform.worldRotation);
}

[[nodiscard]] bool Project(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 position,
    ScreenPoint& output) noexcept {
    return EditorSceneViewportMath::WorldToScreen(camera, renderArea, position, output.x, output.y);
}

[[nodiscard]] std::array<float, 3> ToArray(kb::scene::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] kb::scene::Vec3 ToVec3(const std::array<float, 3>& value) noexcept {
    return kb::scene::Vec3{value[0], value[1], value[2]};
}

[[nodiscard]] kb::render::EditorLightWireframeKind ToWireframeKind(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Spot:
        return kb::render::EditorLightWireframeKind::Spot;
    case kb::scene::LightKind::Directional:
        return kb::render::EditorLightWireframeKind::Directional;
    case kb::scene::LightKind::Point:
    default:
        return kb::render::EditorLightWireframeKind::Point;
    }
}

[[nodiscard]] bool Slab(float origin, float direction, float min, float max, float& nearDistance, float& farDistance) noexcept {
    if (std::abs(direction) <= kRayAabbParallelEpsilon) {
        return origin >= min && origin <= max;
    }

    const float invDirection = 1.0F / direction;
    float t0 = (min - origin) * invDirection;
    float t1 = (max - origin) * invDirection;
    if (t0 > t1) {
        std::swap(t0, t1);
    }

    nearDistance = std::max(nearDistance, t0);
    farDistance = std::min(farDistance, t1);
    return nearDistance <= farDistance;
}

[[nodiscard]] kb::scene::Vec3 BoxExtent(const kb::scene::TransformComponent& transform) noexcept {
    return kb::scene::Vec3{
        std::max(kDefaultPickHalfExtent, std::abs(transform.worldScale.x) * kDefaultPickHalfExtent),
        std::max(kDefaultPickHalfExtent, std::abs(transform.worldScale.y) * kDefaultPickHalfExtent),
        std::max(kDefaultPickHalfExtent, std::abs(transform.worldScale.z) * kDefaultPickHalfExtent),
    };
}

// The renderer publishes, at every scene submit, the same world bounds sphere it culls
// with. It is the only real measurement of where a mesh actually is: the fallback box
// below knows nothing about the asset, so a mesh whose geometry sits away from its own
// origin - a character authored with the origin at its feet - draws entirely outside it.
// Ray directions reaching the picker are normalized (EditorSceneViewportHitResolver), so
// this distance is in the same world units as the slab test's.
// The renderer publishes a box beside the sphere for the same instance, sharing one origin.
// The box is the shape selection wants: a character mesh is typically half as deep as it is
// tall, so its sphere reaches far past the silhouette and steals clicks from empty space.
// Reuses this file's own slab test rather than adding a second ray-box intersection.
[[nodiscard]] bool HitBoundsBox(
    const EditorSceneViewportRay& ray,
    const kb::scene::SceneRenderBounds& bounds,
    float& distance) noexcept {
    const kb::scene::Vec3 localOrigin = EditorSceneViewportMath::Sub(ray.origin, bounds.center);
    float nearDistance = 0.0F;
    float farDistance = 1000000.0F;
    if (!Slab(localOrigin.x, ray.direction.x, -bounds.halfExtents.x, bounds.halfExtents.x, nearDistance, farDistance) ||
        !Slab(localOrigin.y, ray.direction.y, -bounds.halfExtents.y, bounds.halfExtents.y, nearDistance, farDistance) ||
        !Slab(localOrigin.z, ray.direction.z, -bounds.halfExtents.z, bounds.halfExtents.z, nearDistance, farDistance)) {
        return false;
    }

    distance = nearDistance > 0.0F ? nearDistance : farDistance;
    return distance > 0.0F;
}

[[nodiscard]] bool HitBoundsSphere(
    const EditorSceneViewportRay& ray,
    const kb::scene::SceneRenderBounds& bounds,
    float& distance) noexcept {
    const kb::scene::Vec3 toCenter = EditorSceneViewportMath::Sub(bounds.center, ray.origin);
    const float alongRay = EditorSceneViewportMath::Dot(toCenter, ray.direction);
    const float radiusSquared = bounds.radius * bounds.radius;
    const float perpendicularSquared =
        EditorSceneViewportMath::Dot(toCenter, toCenter) - alongRay * alongRay;
    if (perpendicularSquared > radiusSquared) {
        return false;
    }

    const float halfChord = std::sqrt(std::max(0.0F, radiusSquared - perpendicularSquared));
    const float nearDistance = alongRay - halfChord;
    const float farDistance = alongRay + halfChord;
    distance = nearDistance > 0.0F ? nearDistance : farDistance;
    return distance > 0.0F;
}

[[nodiscard]] bool HitTransformBox(
    const EditorSceneViewportRay& ray, const kb::scene::TransformComponent& transform, kb::scene::Vec3 position, float& distance) noexcept {
    const kb::scene::Quat worldRotation = ResolveWorldRotation(transform);
    const kb::scene::Vec3 localOrigin = InverseRotate(worldRotation, EditorSceneViewportMath::Sub(ray.origin, position));
    const kb::scene::Vec3 localDirection = InverseRotate(worldRotation, ray.direction);
    const kb::scene::Vec3 extent = BoxExtent(transform);

    float nearDistance = 0.0F;
    float farDistance = 1000000.0F;
    if (!Slab(localOrigin.x, localDirection.x, -extent.x, extent.x, nearDistance, farDistance) ||
        !Slab(localOrigin.y, localDirection.y, -extent.y, extent.y, nearDistance, farDistance) ||
        !Slab(localOrigin.z, localDirection.z, -extent.z, extent.z, nearDistance, farDistance)) {
        return false;
    }

    distance = nearDistance > 0.0F ? nearDistance : farDistance;
    return distance > 0.0F;
}

[[nodiscard]] RECT NormalizeRect(RECT rect) noexcept {
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

[[nodiscard]] bool ContainsPoint(const RECT& rect, float x, float y) noexcept {
    return x >= static_cast<float>(rect.left) && x <= static_cast<float>(rect.right) &&
        y >= static_cast<float>(rect.top) && y <= static_cast<float>(rect.bottom);
}

void PickNearestVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    if (renderer.shadowOnly || renderer.previewHidden) return;
    static_cast<void>(renderer);
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene != nullptr && !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    float distance = 0.0F;
    const kb::scene::SceneRenderBounds bounds =
        pick.scene != nullptr ? kb::scene::SceneRenderFeedback::BoundsRelativeTo(*pick.scene, entity, pick.origin)
                              : kb::scene::SceneRenderBounds{};
    // Tightest available shape wins. The published box is preferred; the sphere covers a mesh
    // whose box the renderer could not resolve; and before the first submit, or in a headless
    // scene, there is no published frame at all, so the transform box stays the honest floor.
    bool hit = false;
    if (bounds.HasBox()) {
        hit = HitBoundsBox(pick.ray, bounds, distance);
    } else if (bounds.IsValid()) {
        hit = HitBoundsSphere(pick.ray, bounds, distance);
    } else {
        hit = HitTransformBox(pick.ray, transform,
            pick.scene != nullptr ? ResolveViewportPosition(*pick.scene, entity, transform, pick.origin) : transform.worldPosition, distance);
    }
    if (!hit) {
        return;
    }

    if (!pick.result.IsValid() || distance < pick.result.distance) {
        pick.result.entity = entity;
        pick.result.distance = distance;
    }
}

// A light picks by the very lines the Scene View draws for its range, and by its icon; the space
// inside the range is empty and selects nothing.
void PickNearestLightVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::LightComponent& light, void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.overlays == nullptr ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }

    const kb::scene::Vec3 position = ResolveViewportPosition(*pick.scene, entity, transform, pick.origin);
    const kb::scene::Quat rotation = ResolveWorldRotation(transform);
    pick.lightLines.clear();
    kb::render::BuildEditorLightWireframeLines(
        kb::render::EditorLightWireframeDesc{
            .kind = ToWireframeKind(light.kind),
            .position = ToArray(position),
            .forward = ToArray(Rotate(rotation, kb::scene::Vec3{0.0F, 0.0F, 1.0F})),
            .right = ToArray(Rotate(rotation, kb::scene::Vec3{1.0F, 0.0F, 0.0F})),
            .up = ToArray(Rotate(rotation, kb::scene::Vec3{0.0F, 1.0F, 0.0F})),
            .range = light.kind == kb::scene::LightKind::Directional ? 0.0F : std::max(0.0F, light.range),
            .outerConeDegrees = light.outerConeDegrees,
        },
        pick.lightLines);
    for (const kb::render::EditorLightWireframeLine& line : pick.lightLines) {
        pick.overlays->ConsiderLine(entity, ToVec3(line.from), ToVec3(line.to));
    }
    pick.overlays->ConsiderIcon(entity, position, kIconHalfSizePixels);
}

// A camera picks by the frustum lines the Scene View draws for it; its icon is offered by the
// entity visitor below.
void PickNearestCameraVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::CameraComponent& camera, void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.overlays == nullptr ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }

    const kb::scene::Quat rotation = ResolveWorldRotation(transform);
    const auto lines = kb::render::BuildEditorCameraWireframeLines(kb::render::EditorCameraWireframeDesc{
        .projection = camera.projection == kb::scene::CameraProjection::Orthographic
            ? kb::render::EditorCameraWireframeProjection::Orthographic
            : kb::render::EditorCameraWireframeProjection::Perspective,
        .position = ToArray(ResolveViewportPosition(*pick.scene, entity, transform, pick.origin)),
        .forward = ToArray(Rotate(rotation, kb::scene::Vec3{0.0F, 0.0F, 1.0F})),
        .right = ToArray(Rotate(rotation, kb::scene::Vec3{1.0F, 0.0F, 0.0F})),
        .up = ToArray(Rotate(rotation, kb::scene::Vec3{0.0F, 1.0F, 0.0F})),
        .verticalFovDegrees = camera.verticalFovDegrees,
        .orthographicHeight = camera.orthographicHeight,
        .nearClip = camera.nearClip,
        .farClip = camera.farClip,
        .displayFarClip = kb::render::EditorCameraWireframeDisplayFarClip(camera.nearClip, camera.farClip),
        .aspect = 1.0F,
    });
    for (const kb::render::EditorCameraWireframeLine& line : lines) {
        pick.overlays->ConsiderLine(entity, ToVec3(line.from), ToVec3(line.to));
    }
}

// Collider, character controller and joint outlines, while physics debug drawing shows them.
void PickNearestPhysicsLines(NearestPickContext& pick) {
    if (pick.scene == nullptr || pick.overlays == nullptr || !kb::scene::PhysicsDebugDraw::IsEnabled(*pick.scene)) {
        return;
    }
    for (const kb::scene::PhysicsDebugLineDesc& line : kb::scene::PhysicsDebugDraw::CollectLines(*pick.scene, pick.origin)) {
        if (line.owner.IsValid() && kb::scene::ResolveVisibility(*pick.scene, line.owner).visible) {
            pick.overlays->ConsiderLine(line.owner, line.from, line.to);
        }
    }
}

void PickNearestParticleVisitor(
    kb::scene::SceneEntity entity,
    const kb::scene::ParticleEffectComponent&,
    void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.overlays == nullptr ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    pick.overlays->ConsiderIcon(
        entity,
        ResolveViewportPosition(*pick.scene, entity, pick.scene->Transforms().Get(entity), pick.origin),
        kIconHalfSizePixels);
}

[[nodiscard]] bool HasDedicatedPickRepresentation(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    const kb::scene::SceneComponentQueries components = scene.Components();
    return components.MeshRenderers().Has(entity) || components.Lights().Has(entity) ||
        components.ParticleEffects().Has(entity);
}

// Every other entity (a camera, an audio source, a script or physics object, a group) is drawn as
// an icon at its origin and picks by that icon.
void PickNearestEntityVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.overlays == nullptr ||
        HasDedicatedPickRepresentation(*pick.scene, entity) ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    pick.overlays->ConsiderIcon(
        entity, ResolveViewportPosition(*pick.scene, entity, transform, pick.origin), kIconHalfSizePixels);
}

// Rectangle selection on the entity origin alone misses any mesh whose origin sits outside
// the drag - a character authored with its origin at the feet is not selectable by a rectangle
// drawn over its body. Projecting the eight corners of the published world box and testing the
// screen rectangle they span keeps click and rectangle selection on the same representation.
// Corners behind the camera do not project; the rectangle they would have widened is simply
// not counted, which can only make the test stricter, never looser.
[[nodiscard]] bool RectOverlapsProjectedBox(
    const RectPickContext& pick,
    const kb::scene::SceneRenderBounds& bounds) noexcept {
    float minX = 0.0F;
    float minY = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;
    bool projectedAny = false;
    for (std::uint32_t corner = 0U; corner < 8U; ++corner) {
        const kb::scene::Vec3 position{
            bounds.center.x + ((corner & 1U) != 0U ? bounds.halfExtents.x : -bounds.halfExtents.x),
            bounds.center.y + ((corner & 2U) != 0U ? bounds.halfExtents.y : -bounds.halfExtents.y),
            bounds.center.z + ((corner & 4U) != 0U ? bounds.halfExtents.z : -bounds.halfExtents.z),
        };
        ScreenPoint screen{};
        if (!Project(*pick.camera, pick.renderArea, position, screen)) {
            continue;
        }
        if (!projectedAny) {
            minX = screen.x;
            maxX = screen.x;
            minY = screen.y;
            maxY = screen.y;
            projectedAny = true;
            continue;
        }
        minX = std::min(minX, screen.x);
        maxX = std::max(maxX, screen.x);
        minY = std::min(minY, screen.y);
        maxY = std::max(maxY, screen.y);
    }
    if (!projectedAny) {
        return false;
    }

    const RECT& rect = pick.selectionRect;
    return maxX >= static_cast<float>(rect.left) && minX <= static_cast<float>(rect.right) &&
        maxY >= static_cast<float>(rect.top) && minY <= static_cast<float>(rect.bottom);
}

void ConsiderRectTransform(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    RectPickContext& pick) {
    if (pick.scene == nullptr || pick.camera == nullptr || !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    float screenX = 0.0F;
    float screenY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(*pick.camera, pick.renderArea,
            ResolveViewportPosition(*pick.scene, entity, transform, pick.origin), screenX, screenY)) {
        return;
    }

    if (ContainsPoint(pick.selectionRect, screenX, screenY)) {
        pick.entities.push_back(entity);
    }
}

void PickRectVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    if (renderer.shadowOnly || renderer.previewHidden) return;
    auto& pick = *static_cast<RectPickContext*>(context);
    if (pick.scene != nullptr && pick.camera != nullptr &&
        kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        const kb::scene::SceneRenderBounds bounds =
            kb::scene::SceneRenderFeedback::BoundsRelativeTo(*pick.scene, entity, pick.origin);
        if (bounds.HasBox()) {
            // The box already contains the origin, so a rectangle that misses the box cannot
            // contain the origin either - there is nothing for the origin test to add here.
            if (RectOverlapsProjectedBox(pick, bounds)) {
                pick.entities.push_back(entity);
            }
            return;
        }
    }
    ConsiderRectTransform(entity, transform, pick);
}

void PickRectLightVisitor(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    const kb::scene::LightComponent&,
    void* context) {
    ConsiderRectTransform(entity, transform, *static_cast<RectPickContext*>(context));
}

void PickRectParticleVisitor(
    kb::scene::SceneEntity entity,
    const kb::scene::ParticleEffectComponent&,
    void* context) {
    auto& pick = *static_cast<RectPickContext*>(context);
    if (pick.scene == nullptr || !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    const kb::scene::TransformComponent* transform = pick.scene->Transforms().TryGet(entity);
    if (transform == nullptr) {
        return;
    }
    float screenX = 0.0F;
    float screenY = 0.0F;
    if (EditorSceneViewportMath::WorldToScreen(*pick.camera, pick.renderArea,
            ResolveViewportPosition(*pick.scene, entity, *transform, pick.origin), screenX, screenY) &&
        ContainsPoint(pick.selectionRect, screenX, screenY)) {
        pick.entities.push_back(entity);
    }
}

void PickRectEntityVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context) {
    auto& pick = *static_cast<RectPickContext*>(context);
    if (pick.scene == nullptr || HasDedicatedPickRepresentation(*pick.scene, entity)) {
        return;
    }
    ConsiderRectTransform(entity, transform, pick);
}

void Deduplicate(std::vector<kb::scene::SceneEntity>& entities) {
    std::ranges::sort(entities, {}, &kb::scene::SceneEntity::Id);
    entities.erase(std::ranges::unique(entities).begin(), entities.end());
}

} // namespace

EditorSceneViewportPickResult EditorSceneViewportMeshPicker::PickNearest(
    kb::scene::Scene& scene, const EditorSceneViewportRay& ray, const kb::math::DVec3& origin) {
    scene.Runtime().SynchronizeTransforms();
    NearestPickContext context{.ray = ray, .scene = &scene, .origin = origin};
    scene.Components().Visitors().ForEachMeshRenderer(&PickNearestVisitor, &context);
    return context.result;
}

EditorSceneViewportPickResult EditorSceneViewportMeshPicker::PickNearest(
    kb::scene::Scene& scene,
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    float screenX,
    float screenY,
    const EditorSceneViewportRay& ray,
    float dpiScale) {
    scene.Runtime().SynchronizeTransforms();
    EditorViewportOverlayPicker overlays(
        camera, renderArea, screenX, screenY, EditorViewportWirePickTolerancePixels(dpiScale));
    NearestPickContext context{
        .ray = ray,
        .scene = &scene,
        .origin = camera.ViewportOrigin(),
        .overlays = &overlays,
    };
    scene.Components().Visitors().ForEachMeshRenderer(&PickNearestVisitor, &context);
    scene.Components().Visitors().ForEachLight(&PickNearestLightVisitor, &context);
    scene.Components().Visitors().ForEachCamera(&PickNearestCameraVisitor, &context);
    PickNearestPhysicsLines(context);
    static_cast<const kb::scene::Scene&>(scene).Components().ParticleEffects().ForEach(
        &PickNearestParticleVisitor, &context);
    scene.Transforms().ForEach(&PickNearestEntityVisitor, &context);
    return ResolveViewportClickPick(context.result, overlays.Best());
}

std::vector<kb::scene::SceneEntity> EditorSceneViewportMeshPicker::PickInsideRect(
    kb::scene::Scene& scene,
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    const RECT& selectionRect) {
    scene.Runtime().SynchronizeTransforms();
    RectPickContext context{
        .scene = &scene,
        .camera = &camera,
        .origin = camera.ViewportOrigin(),
        .renderArea = renderArea,
        .selectionRect = NormalizeRect(selectionRect),
    };
    scene.Components().Visitors().ForEachMeshRenderer(&PickRectVisitor, &context);
    scene.Components().Visitors().ForEachLight(&PickRectLightVisitor, &context);
    static_cast<const kb::scene::Scene&>(scene).Components().ParticleEffects().ForEach(
        &PickRectParticleVisitor, &context);
    scene.Transforms().ForEach(&PickRectEntityVisitor, &context);
    Deduplicate(context.entities);
    return context.entities;
}

} // namespace kb::editor

#endif
