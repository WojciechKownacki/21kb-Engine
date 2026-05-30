#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class Scene;

class SceneTransformQueries {
public:
    explicit SceneTransformQueries(const Scene& scene) noexcept;

    [[nodiscard]] TransformComponent Get(SceneObject object) const;
    [[nodiscard]] TransformComponent Get(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(ConstTransformVisitor visitor, void* context = nullptr) const;

private:
    const Scene& scene_;
};

class SceneTransforms {
public:
    explicit SceneTransforms(Scene& scene) noexcept;

    [[nodiscard]] TransformComponent Get(SceneObject object) const;
    [[nodiscard]] TransformComponent Get(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneObject object, const TransformComponent& transform);
    void Set(SceneEntity entity, const TransformComponent& transform);
    void MarkModified(SceneEntity entity) noexcept;

    void ForEach(ConstTransformVisitor visitor, void* context = nullptr) const;
    void ForEachMutable(MutableTransformVisitor visitor, void* context = nullptr);

private:
    Scene& scene_;
};

} // namespace kb::scene
