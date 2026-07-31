#include "scene/components/SceneSpaceStrokeComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneSpaceStrokeComponentStore::SceneSpaceStrokeComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneSpaceStrokeComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<SpaceStrokeComponent>(world_, entity); }
const SpaceStrokeComponent* SceneSpaceStrokeComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<SpaceStrokeComponent>(world_, entity); }
SpaceStrokeComponent* SceneSpaceStrokeComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<SpaceStrokeComponent>(world_, entity); }
void SceneSpaceStrokeComponentStore::ForEach(SpaceStrokeVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<SpaceStrokeComponent> query = world_->CreateQuery<SpaceStrokeComponent>();
    kb::ecs::UnsafeHotReadQuery<SpaceStrokeComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const SpaceStrokeComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneSpaceStrokeComponentStore::Set(SceneEntity entity, const SpaceStrokeComponent& component) { SceneComponentStorageAccess::Set<SpaceStrokeComponent>(world_, entity, component); }
void SceneSpaceStrokeComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<SpaceStrokeComponent>(world_, entity); }
void SceneSpaceStrokeComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<SpaceStrokeComponent>(world_, entity); }

} // namespace kb::scene
