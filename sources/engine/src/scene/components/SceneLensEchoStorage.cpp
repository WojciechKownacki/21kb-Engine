#include "scene/components/SceneLensEchoComponentStore.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneLensEchoComponentStore::SceneLensEchoComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneLensEchoComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<LensEchoComponent>(world_, entity); }
const LensEchoComponent* SceneLensEchoComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<LensEchoComponent>(world_, entity); }
LensEchoComponent* SceneLensEchoComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<LensEchoComponent>(world_, entity); }
void SceneLensEchoComponentStore::ForEach(LensEchoVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) return;
    kb::ecs::Query<LensEchoComponent> query = world_->CreateQuery<LensEchoComponent>();
    kb::ecs::UnsafeHotReadQuery<LensEchoComponent> hotQuery;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hotQuery.Rebuild(query, settings)) return;
    hotQuery.ForEachRange(settings.maxBatchSize, [visitor, context](const auto& chunk) {
        const LensEchoComponent* components = chunk.template Components<0>();
        for (std::size_t index = 0U; index < chunk.Count(); ++index) visitor(chunk.EntityAt(index), components[index], context);
    });
}
void SceneLensEchoComponentStore::Set(SceneEntity entity, const LensEchoComponent& component) { SceneComponentStorageAccess::Set<LensEchoComponent>(world_, entity, component); }
void SceneLensEchoComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<LensEchoComponent>(world_, entity); }
void SceneLensEchoComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<LensEchoComponent>(world_, entity); }

} // namespace kb::scene
