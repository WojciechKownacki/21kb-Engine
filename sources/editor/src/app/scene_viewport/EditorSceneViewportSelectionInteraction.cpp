#include "app/scene_viewport/EditorSceneViewportSelectionInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace kb::editor {
namespace {

constexpr float kDefaultPickRadius = 1.0F;

struct PickContext {
    EditorSceneViewportRay ray{};
    kb::scene::SceneEntity entity{};
    float distance = 0.0F;
    bool hit = false;
};

[[nodiscard]] bool HitSphere(const EditorSceneViewportRay& ray, kb::scene::Vec3 center, float radius, float& distance) noexcept {
    const kb::scene::Vec3 toCenter = EditorSceneViewportMath::Sub(center, ray.origin);
    const float projected = EditorSceneViewportMath::Dot(toCenter, ray.direction);
    if (projected <= 0.0F) {
        return false;
    }

    const float closestDistanceSquared = EditorSceneViewportMath::LengthSquared(EditorSceneViewportMath::Sub(toCenter, EditorSceneViewportMath::Mul(ray.direction, projected)));
    const float radiusSquared = radius * radius;
    if (closestDistanceSquared > radiusSquared) {
        return false;
    }

    distance = projected - std::sqrt(std::max(0.0F, radiusSquared - closestDistanceSquared));
    return distance > 0.0F;
}

void PickMeshVisitor(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    static_cast<void>(renderer);
    auto& pick = *static_cast<PickContext*>(context);
    float distance = 0.0F;
    if (!HitSphere(pick.ray, transform.localPosition, kDefaultPickRadius, distance)) {
        return;
    }

    if (!pick.hit || distance < pick.distance) {
        pick.entity = entity;
        pick.distance = distance;
        pick.hit = true;
    }
}

} // namespace

bool EditorSceneViewportSelectionInteraction::SelectAt(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveGroundHit(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    PickContext pick{.ray = hit->ray};
    sceneContext.Scene().Components().Visitors().ForEachMeshRenderer(&PickMeshVisitor, &pick);
    if (!pick.hit) {
        sceneContext.ClearHierarchySelection();
        return true;
    }

    sceneContext.SelectEntity(pick.entity);
    return true;
}

} // namespace kb::editor

#endif
