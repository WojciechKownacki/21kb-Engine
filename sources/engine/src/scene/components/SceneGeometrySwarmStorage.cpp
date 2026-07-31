#include "scene/components/SceneGeometrySwarmComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneGeometrySwarmComponentStore::SceneGeometrySwarmComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneGeometrySwarmComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<GeometrySwarmComponent>(world_, entity); }
const GeometrySwarmComponent* SceneGeometrySwarmComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<GeometrySwarmComponent>(world_, entity); }
GeometrySwarmComponent* SceneGeometrySwarmComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<GeometrySwarmComponent>(world_, entity); }
void SceneGeometrySwarmComponentStore::ForEach(GeometrySwarmVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<GeometrySwarmComponent> query = world_->CreateQuery<GeometrySwarmComponent>();
    kb::ecs::UnsafeHotReadQuery<GeometrySwarmComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const GeometrySwarmComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneGeometrySwarmComponentStore::Set(SceneEntity entity, const GeometrySwarmComponent& component) { SceneComponentStorageAccess::Set<GeometrySwarmComponent>(world_, entity, component); }
void SceneGeometrySwarmComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<GeometrySwarmComponent>(world_, entity); }
void SceneGeometrySwarmComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<GeometrySwarmComponent>(world_, entity); }

} // namespace kb::scene
