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

} // namespace kb::scene
