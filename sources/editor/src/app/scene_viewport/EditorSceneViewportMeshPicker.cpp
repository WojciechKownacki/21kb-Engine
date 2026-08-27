#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"

#if defined(_WIN32)
#include "engine/math/EngineMath.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kb::editor {
namespace {

constexpr float kDefaultPickHalfExtent = 1.0F;
constexpr float kRayAabbParallelEpsilon = 0.00001F;
constexpr float kLightWirePickThresholdPixels = 16.0F;
constexpr float kLightWirePickTieEpsilon = 0.25F;
constexpr float kLightIconPickRadiusPixels = 30.0F;
// An entity whose only components are non-visual (script, rigidbody, collider) or that
// merely groups children draws no overlay, so its pick target is its projected origin.
// Kept tighter than the drawn icons: it is an unmarked target, and a generous radius
// would let empty space steal clicks.
constexpr float kEntityOriginPickRadiusPixels = 18.0F;
constexpr float kPi = 3.14159265358979323846F;

struct ScreenPoint {
    float x = 0.0F;
    float y = 0.0F;
};

struct OverlayPickCandidate {
    kb::scene::SceneEntity entity{};
    float distance = 0.0F;
    float score = 0.0F;
    float radius = 0.0F;
    bool blocksMesh = false;

    [[nodiscard]] bool IsValid() const noexcept {
        return entity.IsValid();
    }
};

struct NearestPickContext {
    EditorSceneViewportRay ray{};
    EditorSceneViewportPickResult result{};
    const kb::scene::Scene* scene = nullptr;
    const EditorViewportCameraState* camera = nullptr;
    RECT renderArea{};
    ScreenPoint mouse{};
    OverlayPickCandidate overlayPick{};
};

struct RectPickContext {
    const kb::scene::Scene* scene = nullptr;
    const EditorViewportCameraState* camera = nullptr;
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

[[nodiscard]] kb::scene::Vec3 ResolveWorldPosition(const kb::scene::TransformComponent& transform) noexcept {
    return transform.worldPosition;
}

[[nodiscard]] kb::scene::Quat ResolveWorldRotation(const kb::scene::TransformComponent& transform) noexcept {
    return Normalize(transform.worldRotation);
}

[[nodiscard]] float Distance(ScreenPoint lhs, ScreenPoint rhs) noexcept {
    const float dx = lhs.x - rhs.x;
    const float dy = lhs.y - rhs.y;
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] float SegmentDistance(ScreenPoint point, ScreenPoint a, ScreenPoint b) noexcept {
    const float abX = b.x - a.x;
    const float abY = b.y - a.y;
    const float lengthSquared = abX * abX + abY * abY;
    if (lengthSquared <= 0.000001F) {
        return Distance(point, a);
    }
    const float t = std::clamp(((point.x - a.x) * abX + (point.y - a.y) * abY) / lengthSquared, 0.0F, 1.0F);
    return Distance(point, ScreenPoint{a.x + abX * t, a.y + abY * t});
}

[[nodiscard]] bool Project(
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    kb::scene::Vec3 position,
    ScreenPoint& output) noexcept {
    return EditorSceneViewportMath::WorldToScreen(camera, renderArea, position, output.x, output.y);
}

[[nodiscard]] bool BetterOverlayPick(const OverlayPickCandidate& candidate, const OverlayPickCandidate& current) noexcept {
    if (!current.IsValid()) {
        return true;
    }
    if (candidate.radius + kLightWirePickTieEpsilon < current.radius) {
        return true;
    }
    if (current.radius + kLightWirePickTieEpsilon < candidate.radius) {
        return false;
    }
    if (candidate.score + kLightWirePickTieEpsilon < current.score) {
        return true;
    }
    if (std::abs(candidate.score - current.score) <= kLightWirePickTieEpsilon) {
        return candidate.distance < current.distance;
    }
    return false;
}

void ConsiderLightPick(NearestPickContext& pick, kb::scene::SceneEntity entity, float score, float radius, float distance, bool blocksMesh) {
    if (score > kLightWirePickThresholdPixels || radius <= 0.0F || distance <= 0.0F) {
        return;
    }

    OverlayPickCandidate candidate{
        .entity = entity,
        .distance = distance,
        .score = score,
        .radius = radius,
        .blocksMesh = blocksMesh,
    };
    if (BetterOverlayPick(candidate, pick.overlayPick)) {
        pick.overlayPick = candidate;
    }
}

void ConsiderLightVolumePick(NearestPickContext& pick, kb::scene::SceneEntity entity, ScreenPoint center, float radius, float distance) {
    if (Distance(pick.mouse, center) > radius || radius <= 0.0F || distance <= 0.0F) {
        return;
    }

    OverlayPickCandidate candidate{
        .entity = entity,
        .distance = distance,
        .score = 0.0F,
        .radius = radius,
        .blocksMesh = false,
    };
    if (BetterOverlayPick(candidate, pick.overlayPick)) {
        pick.overlayPick = candidate;
    }
}

void ConsiderOverlayIconPick(NearestPickContext& pick, kb::scene::SceneEntity entity, ScreenPoint center, float tieRadius, float distance) {
    const float score = Distance(pick.mouse, center);
    if (score > kLightIconPickRadiusPixels || distance <= 0.0F) {
        return;
    }

    OverlayPickCandidate candidate{
        .entity = entity,
        .distance = distance,
        .score = score,
        .radius = std::max(1.0F, tieRadius),
        .blocksMesh = true,
    };
    if (BetterOverlayPick(candidate, pick.overlayPick)) {
        pick.overlayPick = candidate;
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

[[nodiscard]] bool HitTransformBox(const EditorSceneViewportRay& ray, const kb::scene::TransformComponent& transform, float& distance) noexcept {
    const kb::scene::Quat worldRotation = ResolveWorldRotation(transform);
    const kb::scene::Vec3 localOrigin = InverseRotate(worldRotation, EditorSceneViewportMath::Sub(ray.origin, transform.worldPosition));
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
    static_cast<void>(renderer);
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene != nullptr && !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    float distance = 0.0F;
    if (!HitTransformBox(pick.ray, transform, distance)) {
        return;
    }

    if (!pick.result.IsValid() || distance < pick.result.distance) {
        pick.result.entity = entity;
        pick.result.distance = distance;
    }
}

void PickNearestLightVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::LightComponent& light, void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.camera == nullptr ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }

    const kb::scene::Vec3 position = ResolveWorldPosition(transform);
    const EditorViewportCameraAxes cameraAxes = pick.camera->Axes();
    const float cameraDistance = EditorSceneViewportMath::Dot(EditorSceneViewportMath::Sub(position, cameraAxes.position), cameraAxes.forward);
    if (cameraDistance <= 0.0F) {
        return;
    }

    ScreenPoint center{};
    if (!Project(*pick.camera, pick.renderArea, position, center)) {
        return;
    }

    if (light.kind == kb::scene::LightKind::Directional || light.range <= 0.0F) {
        ConsiderOverlayIconPick(pick, entity, center, kLightIconPickRadiusPixels, cameraDistance);
        return;
    }

    if (light.kind == kb::scene::LightKind::Point) {
        ScreenPoint right{};
        ScreenPoint up{};
        if (!Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(position, EditorSceneViewportMath::Mul(cameraAxes.right, light.range)), right) ||
            !Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(position, EditorSceneViewportMath::Mul(cameraAxes.up, light.range)), up)) {
            return;
        }

        const float screenRadius = std::max(Distance(center, right), Distance(center, up));
        ConsiderOverlayIconPick(pick, entity, center, screenRadius, cameraDistance);
        const float wireScore = std::abs(Distance(pick.mouse, center) - screenRadius);
        ConsiderLightPick(pick, entity, wireScore, screenRadius, cameraDistance, true);
        ConsiderLightVolumePick(pick, entity, center, screenRadius, cameraDistance);
        return;
    }

    const kb::scene::Quat rotation = ResolveWorldRotation(transform);
    const kb::scene::Vec3 forward = Rotate(rotation, kb::scene::Vec3{0.0F, 0.0F, 1.0F});
    const kb::scene::Vec3 rightAxis = Rotate(rotation, kb::scene::Vec3{1.0F, 0.0F, 0.0F});
    const kb::scene::Vec3 upAxis = Rotate(rotation, kb::scene::Vec3{0.0F, 1.0F, 0.0F});
    const float range = std::max(0.01F, light.range);
    const float coneRadians = std::clamp(light.outerConeDegrees, 0.0F, 179.0F) * 0.5F * kPi / 180.0F;
    const float coneRadius = std::tan(coneRadians) * range;
    const kb::scene::Vec3 endCenter = EditorSceneViewportMath::Add(position, EditorSceneViewportMath::Mul(forward, range));

    ScreenPoint ringCenter{};
    ScreenPoint ringRight{};
    ScreenPoint ringUp{};
    if (!Project(*pick.camera, pick.renderArea, endCenter, ringCenter) ||
        !Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(endCenter, EditorSceneViewportMath::Mul(rightAxis, coneRadius)), ringRight) ||
        !Project(*pick.camera, pick.renderArea, EditorSceneViewportMath::Add(endCenter, EditorSceneViewportMath::Mul(upAxis, coneRadius)), ringUp)) {
        return;
    }

    const float screenRadius = std::max(Distance(ringCenter, ringRight), Distance(ringCenter, ringUp));
    ConsiderOverlayIconPick(pick, entity, center, screenRadius, cameraDistance);
    float score = std::abs(Distance(pick.mouse, ringCenter) - screenRadius);
    for (std::uint32_t i = 0; i < 4U; ++i) {
        const float angle = static_cast<float>(i) * kPi * 0.5F + kPi * 0.25F;
        const kb::scene::Vec3 rim = EditorSceneViewportMath::Add(
            endCenter,
            EditorSceneViewportMath::Add(
                EditorSceneViewportMath::Mul(rightAxis, std::cos(angle) * coneRadius),
                EditorSceneViewportMath::Mul(upAxis, std::sin(angle) * coneRadius)));
        ScreenPoint rimScreen{};
        if (Project(*pick.camera, pick.renderArea, rim, rimScreen)) {
            score = std::min(score, SegmentDistance(pick.mouse, center, rimScreen));
        }
    }
    ConsiderLightPick(pick, entity, score, screenRadius, cameraDistance, true);
}

void PickNearestParticleVisitor(
    kb::scene::SceneEntity entity,
    const kb::scene::ParticleEffectComponent&,
    void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.camera == nullptr ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    const kb::scene::Vec3 position = ResolveWorldPosition(
        pick.scene->Transforms().Get(entity));
    const EditorViewportCameraAxes cameraAxes = pick.camera->Axes();
    const float cameraDistance = EditorSceneViewportMath::Dot(
        EditorSceneViewportMath::Sub(position, cameraAxes.position),
        cameraAxes.forward);
    ScreenPoint center{};
    if (cameraDistance <= 0.0F ||
        !Project(*pick.camera, pick.renderArea, position, center)) {
        return;
    }
    ConsiderOverlayIconPick(
        pick, entity, center, kLightIconPickRadiusPixels, cameraDistance);
}

[[nodiscard]] bool HasDedicatedPickRepresentation(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    const kb::scene::SceneComponentQueries components = scene.Components();
    return components.MeshRenderers().Has(entity) || components.Lights().Has(entity) ||
        components.ParticleEffects().Has(entity);
}

void ConsiderEntityOriginPick(NearestPickContext& pick, kb::scene::SceneEntity entity, ScreenPoint center, float distance) {
    const float score = Distance(pick.mouse, center);
    if (score > kEntityOriginPickRadiusPixels || distance <= 0.0F) {
        return;
    }

    OverlayPickCandidate candidate{
        .entity = entity,
        .distance = distance,
        .score = score,
        // Tie-break as the widest overlay so a drawn icon or wireframe of comparable score
        // wins: a visible target is always the better answer to the same click.
        .radius = kLightIconPickRadiusPixels,
        // A mesh in front must keep the click, otherwise every parent or grouping node
        // sharing a mesh origin would steal it.
        .blocksMesh = false,
    };
    if (BetterOverlayPick(candidate, pick.overlayPick)) {
        pick.overlayPick = candidate;
    }
}

void PickNearestEntityVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, void* context) {
    auto& pick = *static_cast<NearestPickContext*>(context);
    if (pick.scene == nullptr || pick.camera == nullptr ||
        HasDedicatedPickRepresentation(*pick.scene, entity) ||
        !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }

    const kb::scene::Vec3 position = ResolveWorldPosition(transform);
    const EditorViewportCameraAxes cameraAxes = pick.camera->Axes();
    const float cameraDistance = EditorSceneViewportMath::Dot(
        EditorSceneViewportMath::Sub(position, cameraAxes.position), cameraAxes.forward);
    ScreenPoint center{};
    if (cameraDistance <= 0.0F || !Project(*pick.camera, pick.renderArea, position, center)) {
        return;
    }
    ConsiderEntityOriginPick(pick, entity, center, cameraDistance);
}

void ConsiderRectTransform(
    kb::scene::SceneEntity entity,
    const kb::scene::TransformComponent& transform,
    RectPickContext& pick) {
    if (pick.scene == nullptr || !kb::scene::ResolveVisibility(*pick.scene, entity).visible) {
        return;
    }
    float screenX = 0.0F;
    float screenY = 0.0F;
    if (!EditorSceneViewportMath::WorldToScreen(*pick.camera, pick.renderArea, transform.worldPosition, screenX, screenY)) {
        return;
    }

    if (ContainsPoint(pick.selectionRect, screenX, screenY)) {
        pick.entities.push_back(entity);
    }
}

void PickRectVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent&, void* context) {
    ConsiderRectTransform(entity, transform, *static_cast<RectPickContext*>(context));
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
    if (EditorSceneViewportMath::WorldToScreen(*pick.camera, pick.renderArea, transform->worldPosition, screenX, screenY) &&
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

EditorSceneViewportPickResult EditorSceneViewportMeshPicker::PickNearest(kb::scene::Scene& scene, const EditorSceneViewportRay& ray) {
    scene.Runtime().SynchronizeTransforms();
    NearestPickContext context{.ray = ray, .scene = &scene};
    scene.Components().Visitors().ForEachMeshRenderer(&PickNearestVisitor, &context);
    return context.result;
}

EditorSceneViewportPickResult EditorSceneViewportMeshPicker::PickNearest(
    kb::scene::Scene& scene,
    const EditorViewportCameraState& camera,
    const RECT& renderArea,
    float screenX,
    float screenY,
    const EditorSceneViewportRay& ray) {
    scene.Runtime().SynchronizeTransforms();
    NearestPickContext context{
        .ray = ray,
        .scene = &scene,
        .camera = &camera,
        .renderArea = renderArea,
        .mouse = ScreenPoint{screenX, screenY},
    };
    scene.Components().Visitors().ForEachMeshRenderer(&PickNearestVisitor, &context);
    scene.Components().Visitors().ForEachLight(&PickNearestLightVisitor, &context);
    static_cast<const kb::scene::Scene&>(scene).Components().ParticleEffects().ForEach(
        &PickNearestParticleVisitor, &context);
    scene.Transforms().ForEach(&PickNearestEntityVisitor, &context);
    if (context.overlayPick.IsValid() &&
        (context.overlayPick.blocksMesh || !context.result.IsValid())) {
        return EditorSceneViewportPickResult{
            .entity = context.overlayPick.entity,
            .distance = context.overlayPick.distance,
        };
    }
    return context.result;
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
