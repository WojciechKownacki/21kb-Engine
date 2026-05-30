#include "scene/components/SceneMeshRendererComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneMeshRendererComponentStore::SceneMeshRendererComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneMeshRendererComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const MeshRendererComponent* SceneMeshRendererComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<MeshRendererComponent>(world_, entity, componentId_);
}

MeshRendererComponent* SceneMeshRendererComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<MeshRendererComponent>(world_, entity, componentId_);
}

void SceneMeshRendererComponentStore::Set(SceneEntity entity, const MeshRendererComponent& renderer) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, renderer);
}

void SceneMeshRendererComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneMeshRendererComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
