#include "scene/SceneEntityService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityDestructionService.hpp"

#include <algorithm>

namespace kb::scene {

void SceneEntityService::DestroyObject(Scene& scene, SceneObject object) noexcept {
    SceneEntityDestructionService::DestroyObject(scene, object);
}

void SceneEntityService::DestroyEntity(Scene& scene, SceneEntity entity) noexcept {
    SceneEntityDestructionService::DestroyEntity(scene, entity);
}

void SceneEntityService::QueueDeferredDestroy(Scene& scene, SceneEntity entity) noexcept {
    if (!entity.IsValid() || !IsAlive(scene, entity)) {
        return;
    }
    std::vector<SceneEntity>& queue = SceneAccess::State(scene).pendingDeferredDestroys;
    if (std::ranges::find(queue, entity) == queue.end()) {
        queue.push_back(entity);
    }
}

std::size_t SceneEntityService::DrainDeferredDestroys(Scene& scene) noexcept {
    // Swap out first so a deferred destroy queued from within DestroyEntity's
    // own cascade (were that ever to happen) lands in a fresh queue for the
    // next drain rather than being iterated here mid-loop.
    std::vector<SceneEntity> queued;
    queued.swap(SceneAccess::State(scene).pendingDeferredDestroys);
    std::size_t destroyed = 0U;
    for (const SceneEntity entity : queued) {
        if (IsAlive(scene, entity)) {
            DestroyEntity(scene, entity);
            ++destroyed;
        }
    }
    return destroyed;
}

} // namespace kb::scene
