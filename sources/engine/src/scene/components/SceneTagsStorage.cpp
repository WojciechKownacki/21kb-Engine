#include "scene/components/SceneTagsComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneTagsComponentStore::SceneTagsComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneTagsComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<TagsComponent>(world_, entity);
}

const TagsComponent* SceneTagsComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<TagsComponent>(world_, entity);
}

TagsComponent* SceneTagsComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<TagsComponent>(world_, entity);
}

void SceneTagsComponentStore::Set(SceneEntity entity, const TagsComponent& tags) {
    SceneComponentStorageAccess::Set<TagsComponent>(world_, entity, tags);
}

void SceneTagsComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<TagsComponent>(world_, entity);
}

void SceneTagsComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<TagsComponent>(world_, entity);
}

} // namespace kb::scene
