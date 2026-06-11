#include "scene/components/SceneAudioSourceComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAudioSourceComponentStore::SceneAudioSourceComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneAudioSourceComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const AudioSourceComponent* SceneAudioSourceComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<AudioSourceComponent>(world_, entity, componentId_);
}

AudioSourceComponent* SceneAudioSourceComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<AudioSourceComponent>(world_, entity, componentId_);
}

void SceneAudioSourceComponentStore::Set(SceneEntity entity, const AudioSourceComponent& audioSource) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, audioSource);
}

void SceneAudioSourceComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneAudioSourceComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
