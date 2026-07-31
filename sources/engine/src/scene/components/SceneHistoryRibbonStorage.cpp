#include "scene/components/SceneHistoryRibbonComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneHistoryRibbonComponentStore::SceneHistoryRibbonComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneHistoryRibbonComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<HistoryRibbonComponent>(world_, entity); }
const HistoryRibbonComponent* SceneHistoryRibbonComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<HistoryRibbonComponent>(world_, entity); }
HistoryRibbonComponent* SceneHistoryRibbonComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<HistoryRibbonComponent>(world_, entity); }
void SceneHistoryRibbonComponentStore::ForEach(HistoryRibbonVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<HistoryRibbonComponent> query = world_->CreateQuery<HistoryRibbonComponent>();
    kb::ecs::UnsafeHotReadQuery<HistoryRibbonComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const HistoryRibbonComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneHistoryRibbonComponentStore::Set(SceneEntity entity, const HistoryRibbonComponent& component) { SceneComponentStorageAccess::Set<HistoryRibbonComponent>(world_, entity, component); }
void SceneHistoryRibbonComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<HistoryRibbonComponent>(world_, entity); }
void SceneHistoryRibbonComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<HistoryRibbonComponent>(world_, entity); }

} // namespace kb::scene
