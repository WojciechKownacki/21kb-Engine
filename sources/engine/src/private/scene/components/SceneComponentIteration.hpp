#pragma once

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>
#include <mutex>

struct ecs_query_t;

namespace kb::scene {

// A component query kept across iterations, rebuilt after a structural change of the world.
template <typename... Components>
struct SceneComponentQueryCache {
    std::mutex mutex;
    kb::ecs::Query<Components...> query;
    kb::ecs::UnsafeHotReadQuery<Components...> hotQuery;
};

// The queries of the scene's camera, light and mesh-renderer iterators.
struct SceneComponentIterationQueries {
    SceneComponentQueryCache<CameraComponent, TransformComponent> cameras;
    SceneComponentQueryCache<LightComponent, TransformComponent> lights;
    SceneComponentQueryCache<MeshRendererComponent, TransformComponent> meshRenderers;
};

// Visits the chunks of the query of `Components` with the cached query. A nested or concurrent iteration, which
// finds the cache in use, builds its own query.
template <typename... Components, typename Kernel>
void ForEachCachedQueryRange(const kb::ecs::World& world, SceneComponentQueryCache<Components...>& cache, Kernel&& kernel) {
    kb::ecs::QueryExecutionSettings settings;
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    const std::unique_lock lock{ cache.mutex, std::try_to_lock };
    if (lock.owns_lock()) {
        if (!cache.query.IsValid()) {
            cache.query = const_cast<kb::ecs::World&>(world).CreateQuery<Components...>();
        }
        if (!cache.query.IsValid() || (cache.hotQuery.IsStale(cache.query) && !cache.hotQuery.Rebuild(cache.query, settings))) {
            return;
        }
        static_cast<void>(cache.hotQuery.ForEachRange(0U, kernel));
        return;
    }
    const kb::ecs::Query<Components...> query = const_cast<kb::ecs::World&>(world).CreateQuery<Components...>();
    kb::ecs::UnsafeHotReadQuery<Components...> hotQuery;
    if (query.IsValid() && hotQuery.Rebuild(query, settings)) {
        static_cast<void>(hotQuery.ForEachRange(0U, kernel));
    }
}

class SceneComponentIteration {
public:
    SceneComponentIteration() = delete;

    static void ForEachTransform(const kb::ecs::World& world, std::uint64_t transformComponentId, ConstTransformVisitor visitor, void* context);
    static void ForEachMutableTransform(kb::ecs::World& world, std::uint64_t transformComponentId, MutableTransformVisitor visitor, void* context);
    static void ForEachBehaviour(const kb::ecs::World& world, std::uint64_t behaviourComponentId, BehaviourVisitor visitor, void* context);
    static void ForEachCamera(
        const kb::ecs::World& world,
        std::uint64_t transformComponentId,
        std::uint64_t cameraComponentId,
        SceneComponentIterationQueries& cachedQueries,
        CameraVisitor visitor,
        void* context);
    static void ForEachMeshRenderer(const kb::ecs::World& world, std::uint64_t transformComponentId, std::uint64_t meshRendererComponentId, SceneComponentIterationQueries& cachedQueries, MeshRendererVisitor visitor, void* context);
    static void ForEachVisibleMeshRenderer(
        const kb::ecs::World& world,
        std::uint64_t transformComponentId,
        std::uint64_t visibilityComponentId,
        std::uint64_t meshRendererComponentId,
        SceneComponentIterationQueries& cachedQueries,
        MeshRendererVisitor visitor,
        void* context);
    static void ForEachLight(
        const kb::ecs::World& world,
        std::uint64_t transformComponentId,
        std::uint64_t lightComponentId,
        SceneComponentIterationQueries& cachedQueries,
        LightVisitor visitor,
        void* context);
    static void ForEachPhysicsBody(
        const kb::ecs::World& world,
        std::uint64_t transformComponentId,
        std::uint64_t rigidbodyComponentId,
        std::uint64_t colliderComponentId,
        ecs_query_t*& cachedQuery,
        PhysicsBodyVisitor visitor,
        void* context);
};

} // namespace kb::scene
