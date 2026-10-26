#include "scene/components/SceneBody2DComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneBody2DComponentStore::SceneBody2DComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneBody2DComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<Body2DComponent>(world_, entity);
}
const Body2DComponent* SceneBody2DComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<Body2DComponent>(world_, entity);
}
Body2DComponent* SceneBody2DComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<Body2DComponent>(world_, entity);
}
void SceneBody2DComponentStore::Set(SceneEntity entity, const Body2DComponent& component) {
    SceneComponentStorageAccess::Set<Body2DComponent>(world_, entity, component);
}
void SceneBody2DComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<Body2DComponent>(world_, entity);
}
void SceneBody2DComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<Body2DComponent>(world_, entity);
}

} // namespace kb::scene
