#include "scene/components/SceneRiverComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneRiverComponentStore::SceneRiverComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneRiverComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<RiverComponent>(world_, entity); }
const RiverComponent* SceneRiverComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<RiverComponent>(world_, entity); }
RiverComponent* SceneRiverComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<RiverComponent>(world_, entity); }
void SceneRiverComponentStore::ForEach(RiverVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<RiverComponent> query = world_->CreateQuery<RiverComponent>();
    kb::ecs::UnsafeHotReadQuery<RiverComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const RiverComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneRiverComponentStore::Set(SceneEntity entity, const RiverComponent& component) { SceneComponentStorageAccess::Set<RiverComponent>(world_, entity, component); }
void SceneRiverComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<RiverComponent>(world_, entity); }
void SceneRiverComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<RiverComponent>(world_, entity); }

} // namespace kb::scene
