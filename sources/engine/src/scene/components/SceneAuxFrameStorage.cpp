#include "scene/components/SceneAuxFrameComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"

#include <cstddef>

namespace kb::scene {

SceneAuxFrameComponentStore::SceneAuxFrameComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneAuxFrameComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<AuxFrameComponent>(world_, entity); }
const AuxFrameComponent* SceneAuxFrameComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<AuxFrameComponent>(world_, entity); }
AuxFrameComponent* SceneAuxFrameComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<AuxFrameComponent>(world_, entity); }
void SceneAuxFrameComponentStore::ForEach(AuxFrameVisitor visitor, void* context) const {
    if (world_ == nullptr || visitor == nullptr) {
        return;
    }
    kb::ecs::Query<AuxFrameComponent> query = world_->CreateQuery<AuxFrameComponent>();
    if (!query.IsValid()) {
        return;
    }
    kb::ecs::UnsafeHotReadQuery<AuxFrameComponent> hotQuery;
    if (!hotQuery.Rebuild(query, kb::ecs::QueryExecutionSettings{.policy = kb::ecs::QueryExecutionPolicy::SingleThread})) {
        return;
    }
    hotQuery.ForEachRange(0U, [visitor, context](const kb::ecs::UnsafeHotChunk<AuxFrameComponent>& chunk) {
        const AuxFrameComponent* components = chunk.Components<0>();
        for (std::size_t row = 0U; row < chunk.Count(); ++row) {
            const SceneEntity entity = chunk.EntityAt(row);
            if (entity.IsValid()) {
                visitor(entity, components[row], context);
            }
        }
    });
}
void SceneAuxFrameComponentStore::Set(SceneEntity entity, const AuxFrameComponent& component) { SceneComponentStorageAccess::Set<AuxFrameComponent>(world_, entity, component); }
void SceneAuxFrameComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<AuxFrameComponent>(world_, entity); }
void SceneAuxFrameComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<AuxFrameComponent>(world_, entity); }

} // namespace kb::scene
