#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneVisitorTypes.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class Scene;

class SceneTransformService {
public:
    SceneTransformService() = delete;

    [[nodiscard]] static TransformComponent Get(const Scene& scene, SceneObject object);
    [[nodiscard]] static TransformComponent Get(const Scene& scene, SceneEntity entity);
    [[nodiscard]] static const TransformComponent* TryGet(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static TransformComponent* TryGet(Scene& scene, SceneEntity entity) noexcept;
    static void Set(Scene& scene, SceneObject object, const TransformComponent& transform);
    static void Set(Scene& scene, SceneEntity entity, const TransformComponent& transform);
    static void MarkModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkParentModified(Scene& scene, SceneEntity entity) noexcept;
    static void ForEach(const Scene& scene, ConstTransformVisitor visitor, void* context = nullptr);
    static void ForEachMutable(Scene& scene, MutableTransformVisitor visitor, void* context = nullptr);
};

} // namespace kb::scene
