#include "scene/components/SceneCharacterControllerComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneCharacterControllerComponentStore::SceneCharacterControllerComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneCharacterControllerComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<CharacterControllerComponent>(world_, entity);
}

const CharacterControllerComponent* SceneCharacterControllerComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<CharacterControllerComponent>(world_, entity);
}

CharacterControllerComponent* SceneCharacterControllerComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<CharacterControllerComponent>(world_, entity);
}

void SceneCharacterControllerComponentStore::Set(SceneEntity entity, const CharacterControllerComponent& characterController) {
    SceneComponentStorageAccess::Set<CharacterControllerComponent>(world_, entity, characterController);
}

void SceneCharacterControllerComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<CharacterControllerComponent>(world_, entity);
}

void SceneCharacterControllerComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<CharacterControllerComponent>(world_, entity);
}

} // namespace kb::scene
