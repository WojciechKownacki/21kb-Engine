#include "scene/components/SceneInputComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneInputComponentStore::SceneInputComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneInputComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<InputComponent>(world_, entity);
}

const InputComponent* SceneInputComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<InputComponent>(world_, entity);
}

InputComponent* SceneInputComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<InputComponent>(world_, entity);
}

void SceneInputComponentStore::Set(SceneEntity entity, const InputComponent& input) {
    SceneComponentStorageAccess::Set<InputComponent>(world_, entity, input);
}

void SceneInputComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<InputComponent>(world_, entity);
}

void SceneInputComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<InputComponent>(world_, entity);
}

} // namespace kb::scene
