#include "scene/components/SceneMeshRendererComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneMeshRendererComponentStore::SceneMeshRendererComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world)
    , componentId_(componentId) {}

bool SceneMeshRendererComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<MeshRendererComponent>(world_, entity);
}

const MeshRendererComponent* SceneMeshRendererComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<MeshRendererComponent>(world_, entity);
}

MeshRendererComponent* SceneMeshRendererComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<MeshRendererComponent>(world_, entity);
}

void SceneMeshRendererComponentStore::Set(SceneEntity entity, const MeshRendererComponent& renderer) {
    SceneComponentStorageAccess::Set<MeshRendererComponent>(world_, entity, renderer);
}

void SceneMeshRendererComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<MeshRendererComponent>(world_, entity);
}

void SceneMeshRendererComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<MeshRendererComponent>(world_, entity);
}

} // namespace kb::scene
