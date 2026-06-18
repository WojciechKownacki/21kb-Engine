#include "scene/components/SceneAudioListenerComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAudioListenerComponentStore::SceneAudioListenerComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world)
    , componentId_(componentId) {}

bool SceneAudioListenerComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<AudioListenerComponent>(world_, entity);
}

const AudioListenerComponent* SceneAudioListenerComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<AudioListenerComponent>(world_, entity);
}

AudioListenerComponent* SceneAudioListenerComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<AudioListenerComponent>(world_, entity);
}

void SceneAudioListenerComponentStore::Set(SceneEntity entity, const AudioListenerComponent& audioListener) {
    SceneComponentStorageAccess::Set<AudioListenerComponent>(world_, entity, audioListener);
}

void SceneAudioListenerComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<AudioListenerComponent>(world_, entity);
}

void SceneAudioListenerComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<AudioListenerComponent>(world_, entity);
}

} // namespace kb::scene
