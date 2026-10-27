#include "scene/components/SceneCharacterController2DComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneCharacterController2DComponentStore::SceneCharacterController2DComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool SceneCharacterController2DComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<CharacterController2DComponent>(world_, entity);
}
const CharacterController2DComponent* SceneCharacterController2DComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<CharacterController2DComponent>(world_, entity);
}
CharacterController2DComponent* SceneCharacterController2DComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<CharacterController2DComponent>(world_, entity);
}
void SceneCharacterController2DComponentStore::Set(SceneEntity entity, const CharacterController2DComponent& component) {
    SceneComponentStorageAccess::Set<CharacterController2DComponent>(world_, entity, component);
}
void SceneCharacterController2DComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<CharacterController2DComponent>(world_, entity);
}
void SceneCharacterController2DComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<CharacterController2DComponent>(world_, entity);
}

} // namespace kb::scene
