#include "engine/scene/SceneTransforms.hpp"

#include "scene/SceneTransformService.hpp"

namespace kb::scene {

TransformComponent SceneTransformQueries::Get(SceneObject object) const {
    return SceneTransformService::Get(scene_, object);
}

TransformComponent SceneTransformQueries::Get(SceneEntity entity) const {
    return SceneTransformService::Get(scene_, entity);
}

const TransformComponent* SceneTransformQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

bool SceneTransformQueries::ReadNonAlloc(std::span<const SceneEntity> entities, std::span<TransformComponent> transforms) const noexcept {
    if (transforms.size() < entities.size()) {
        return false;
    }

    bool allFound = true;
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        const TransformComponent* transform = TryGet(entities[index]);
        allFound = allFound && transform != nullptr;
        transforms[index] = transform == nullptr ? TransformComponent{} : *transform;
    }
    return allFound;
}

TransformComponent SceneTransforms::Get(SceneObject object) const {
    return SceneTransformService::Get(scene_, object);
}

TransformComponent SceneTransforms::Get(SceneEntity entity) const {
    return SceneTransformService::Get(scene_, entity);
}

const TransformComponent* SceneTransforms::TryGet(SceneEntity entity) const noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

TransformComponent* SceneTransforms::TryGet(SceneEntity entity) noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

bool SceneTransforms::ReadNonAlloc(std::span<const SceneEntity> entities, std::span<TransformComponent> transforms) const noexcept {
    if (transforms.size() < entities.size()) {
        return false;
    }

    bool allFound = true;
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        const TransformComponent* transform = TryGet(entities[index]);
        allFound = allFound && transform != nullptr;
        transforms[index] = transform == nullptr ? TransformComponent{} : *transform;
    }
    return allFound;
}

} // namespace kb::scene
