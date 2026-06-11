#include "scene/components/SceneAudioListenerComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAudioListenerComponentStore::SceneAudioListenerComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneAudioListenerComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const AudioListenerComponent* SceneAudioListenerComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<AudioListenerComponent>(world_, entity, componentId_);
}

AudioListenerComponent* SceneAudioListenerComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<AudioListenerComponent>(world_, entity, componentId_);
}

void SceneAudioListenerComponentStore::Set(SceneEntity entity, const AudioListenerComponent& audioListener) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, audioListener);
}

void SceneAudioListenerComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneAudioListenerComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
