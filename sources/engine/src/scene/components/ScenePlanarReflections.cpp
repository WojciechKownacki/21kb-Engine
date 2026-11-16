#include "engine/scene/ScenePlanarReflections.hpp"

#include "engine/scene/PlanarReflectionComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"

#include <algorithm>
#include <cmath>

namespace kb::scene {
namespace {

[[nodiscard]] std::array<float, 3> Rotate(const Quat& q, float x, float y, float z) noexcept {
    const float tx = 2.0F * (q.y * z - q.z * y);
    const float ty = 2.0F * (q.z * x - q.x * z);
    const float tz = 2.0F * (q.x * y - q.y * x);
    return { x + q.w * tx + (q.y * tz - q.z * ty), y + q.w * ty + (q.z * tx - q.x * tz), z + q.w * tz + (q.x * ty - q.y * tx) };
}

} // namespace

bool SceneHasPlanarReflections(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return SceneEntityCounter::CountWithComponent(state.world, state.components.PlanarReflectionComponentId()) != 0U;
}

void ScenePlanarReflectionPlanes(const Scene& scene, std::vector<ScenePlanarReflectionPlane>& out) {
    SceneState& state = SceneAccess::State(const_cast<Scene&>(scene));
    const std::size_t first = out.size();
    struct Gather {
        const Scene& scene;
        std::vector<ScenePlanarReflectionPlane>& out;
    } gather{ scene, out };
    state.world.CreateQuery<PlanarReflectionComponent>().ForEach(
        [](SceneEntity entity, const PlanarReflectionComponent& mirror, void* context) {
            auto& gathered = *static_cast<Gather*>(context);
            if (!mirror.enabled || !IsPlanarReflectionComponentValid(mirror)) return;
            const TransformComponent* transform = gathered.scene.Transforms().TryGet(entity);
            if (transform == nullptr) return;
            ScenePlanarReflectionPlane plane;
            plane.entity = entity;
            plane.center = gathered.scene.Transforms().WorldTranslation(entity, *transform);
            plane.right = Rotate(transform->worldRotation, 1.0F, 0.0F, 0.0F);
            plane.normal = Rotate(transform->worldRotation, 0.0F, 1.0F, 0.0F);
            plane.forward = Rotate(transform->worldRotation, 0.0F, 0.0F, 1.0F);
            plane.halfWidth = mirror.width * 0.5F;
            plane.halfDepth = mirror.depth * 0.5F;
            plane.resolutionScale = mirror.resolutionScale;
            plane.reflectedLayers = mirror.reflectedLayers;
            plane.maxDistance = mirror.maxDistance;
            plane.reflectShadows = mirror.reflectShadows;
            gathered.out.push_back(plane);
        }, &gather);
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(), [](const ScenePlanarReflectionPlane& a, const ScenePlanarReflectionPlane& b) {
        return a.entity.Id() < b.entity.Id();
    });
}

} // namespace kb::scene
