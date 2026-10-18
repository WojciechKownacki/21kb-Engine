#include "scene/components/SceneAIBrainComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAIBrainComponentStore::SceneAIBrainComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }

bool SceneAIBrainComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<AIBrainComponent>(world_, entity);
}
const AIBrainComponent* SceneAIBrainComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<AIBrainComponent>(world_, entity);
}
AIBrainComponent* SceneAIBrainComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<AIBrainComponent>(world_, entity);
}
void SceneAIBrainComponentStore::Set(SceneEntity entity, const AIBrainComponent& aiBrain) {
    SceneComponentStorageAccess::Set<AIBrainComponent>(world_, entity, aiBrain);
}
void SceneAIBrainComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<AIBrainComponent>(world_, entity);
}
void SceneAIBrainComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<AIBrainComponent>(world_, entity);
}

} // namespace kb::scene
