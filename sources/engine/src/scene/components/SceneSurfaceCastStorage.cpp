#include "scene/components/SceneSurfaceCastComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneSurfaceCastComponentStore::SceneSurfaceCastComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneSurfaceCastComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<SurfaceCastComponent>(world_, entity); }
const SurfaceCastComponent* SceneSurfaceCastComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<SurfaceCastComponent>(world_, entity); }
SurfaceCastComponent* SceneSurfaceCastComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<SurfaceCastComponent>(world_, entity); }
void SceneSurfaceCastComponentStore::ForEach(SurfaceCastVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<SurfaceCastComponent> query = world_->CreateQuery<SurfaceCastComponent>();
    kb::ecs::UnsafeHotReadQuery<SurfaceCastComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const SurfaceCastComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneSurfaceCastComponentStore::Set(SceneEntity entity, const SurfaceCastComponent& component) { SceneComponentStorageAccess::Set<SurfaceCastComponent>(world_, entity, component); }
void SceneSurfaceCastComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<SurfaceCastComponent>(world_, entity); }
void SceneSurfaceCastComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<SurfaceCastComponent>(world_, entity); }

} // namespace kb::scene
