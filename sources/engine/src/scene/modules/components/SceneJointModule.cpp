#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneJointComponentQueries::SceneJointComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneJointComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasJoint(scene_, entity);
}

const JointComponent* SceneJointComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetJoint(scene_, entity);
}

SceneJointComponents::SceneJointComponents(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneJointComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasJoint(scene_, entity);
}

const JointComponent* SceneJointComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetJoint(scene_, entity);
}

JointComponent* SceneJointComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetJoint(scene_, entity);
}

void SceneJointComponents::Set(SceneEntity entity, const JointComponent& joint) {
    SceneComponentMutationService::SetJoint(scene_, entity, joint);
}

void SceneJointComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveJoint(scene_, entity);
}

void SceneJointComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkJointModified(scene_, entity);
}

} // namespace kb::scene
