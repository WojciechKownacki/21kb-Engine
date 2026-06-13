#include "scene/components/SceneTagsComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneTagsComponentStore::SceneTagsComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneTagsComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const TagsComponent* SceneTagsComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TagsComponent>(world_, entity, componentId_);
}

TagsComponent* SceneTagsComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TagsComponent>(world_, entity, componentId_);
}

void SceneTagsComponentStore::Set(SceneEntity entity, const TagsComponent& tags) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, tags);
}

void SceneTagsComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneTagsComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
