#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {
SceneGridComponentQueries::SceneGridComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneGridComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasGrid(scene_, entity);
}
const GridComponent* SceneGridComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetGrid(scene_, entity);
}
SceneGridComponents::SceneGridComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneGridComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasGrid(scene_, entity);
}
const GridComponent* SceneGridComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetGrid(scene_, entity);
}
GridComponent* SceneGridComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetGrid(scene_, entity);
}
void SceneGridComponents::Set(SceneEntity entity, const GridComponent& component) {
    SceneComponentMutationService::SetGrid(scene_, entity, component);
}
void SceneGridComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveGrid(scene_, entity);
}
void SceneGridComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkGridModified(scene_, entity);
}
} // namespace kb::scene
