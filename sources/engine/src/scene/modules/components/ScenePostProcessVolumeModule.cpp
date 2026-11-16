#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
ScenePostProcessVolumeComponentQueries::ScenePostProcessVolumeComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool ScenePostProcessVolumeComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasPostProcessVolume(scene_, entity);
}
const PostProcessVolumeComponent* ScenePostProcessVolumeComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetPostProcessVolume(scene_, entity);
}
ScenePostProcessVolumeComponents::ScenePostProcessVolumeComponents(Scene& scene) noexcept : scene_(scene) {}
bool ScenePostProcessVolumeComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasPostProcessVolume(scene_, entity);
}
const PostProcessVolumeComponent* ScenePostProcessVolumeComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetPostProcessVolume(scene_, entity);
}
PostProcessVolumeComponent* ScenePostProcessVolumeComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetPostProcessVolume(scene_, entity);
}
void ScenePostProcessVolumeComponents::Set(SceneEntity entity, const PostProcessVolumeComponent& component) {
    SceneComponentMutationService::SetPostProcessVolume(scene_, entity, component);
}
void ScenePostProcessVolumeComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemovePostProcessVolume(scene_, entity);
}
void ScenePostProcessVolumeComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkPostProcessVolumeModified(scene_, entity);
}
} // namespace kb::scene
