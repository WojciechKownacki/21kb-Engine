#include "scene/components/SceneSprite2DComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneSprite2DComponentStore::SceneSprite2DComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneSprite2DComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<Sprite2DComponent>(world_, entity);
}
const Sprite2DComponent* SceneSprite2DComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<Sprite2DComponent>(world_, entity);
}
Sprite2DComponent* SceneSprite2DComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<Sprite2DComponent>(world_, entity);
}
void SceneSprite2DComponentStore::Set(SceneEntity entity, const Sprite2DComponent& component) {
    SceneComponentStorageAccess::Set<Sprite2DComponent>(world_, entity, component);
}
void SceneSprite2DComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<Sprite2DComponent>(world_, entity);
}
void SceneSprite2DComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<Sprite2DComponent>(world_, entity);
}

} // namespace kb::scene
