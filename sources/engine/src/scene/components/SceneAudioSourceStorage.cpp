#include "scene/components/SceneAudioSourceComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAudioSourceComponentStore::SceneAudioSourceComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneAudioSourceComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<AudioSourceComponent>(world_, entity);
}

const AudioSourceComponent* SceneAudioSourceComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<AudioSourceComponent>(world_, entity);
}

AudioSourceComponent* SceneAudioSourceComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<AudioSourceComponent>(world_, entity);
}

void SceneAudioSourceComponentStore::Set(SceneEntity entity, const AudioSourceComponent& audioSource) {
    SceneComponentStorageAccess::Set<AudioSourceComponent>(world_, entity, audioSource);
}

void SceneAudioSourceComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<AudioSourceComponent>(world_, entity);
}

void SceneAudioSourceComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<AudioSourceComponent>(world_, entity);
}

} // namespace kb::scene
