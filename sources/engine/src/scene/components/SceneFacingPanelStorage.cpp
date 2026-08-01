#include "scene/components/SceneFacingPanelComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneFacingPanelComponentStore::SceneFacingPanelComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneFacingPanelComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<FacingPanelComponent>(world_, entity); }
const FacingPanelComponent* SceneFacingPanelComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<FacingPanelComponent>(world_, entity); }
FacingPanelComponent* SceneFacingPanelComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<FacingPanelComponent>(world_, entity); }
void SceneFacingPanelComponentStore::ForEach(FacingPanelVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<FacingPanelComponent> query = world_->CreateQuery<FacingPanelComponent>();
    kb::ecs::UnsafeHotReadQuery<FacingPanelComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const FacingPanelComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneFacingPanelComponentStore::Set(SceneEntity entity, const FacingPanelComponent& component) { SceneComponentStorageAccess::Set<FacingPanelComponent>(world_, entity, component); }
void SceneFacingPanelComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<FacingPanelComponent>(world_, entity); }
void SceneFacingPanelComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<FacingPanelComponent>(world_, entity); }

} // namespace kb::scene
