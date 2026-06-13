#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneTagsComponentQueries::SceneTagsComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneTagsComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTags(scene_, entity);
}

const TagsComponent* SceneTagsComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTags(scene_, entity);
}

SceneTagsComponents::SceneTagsComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneTagsComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasTags(scene_, entity);
}

const TagsComponent* SceneTagsComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetTags(scene_, entity);
}

TagsComponent* SceneTagsComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetTags(scene_, entity);
}

void SceneTagsComponents::Set(SceneEntity entity, const TagsComponent& tags) {
    SceneComponentMutationService::SetTags(scene_, entity, tags);
}

void SceneTagsComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveTags(scene_, entity);
}

void SceneTagsComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkTagsModified(scene_, entity);
}

} // namespace kb::scene
