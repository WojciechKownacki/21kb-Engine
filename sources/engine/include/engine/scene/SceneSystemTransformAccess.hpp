#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class Scene;

class SceneSystemTransformAccess {
public:
    explicit SceneSystemTransformAccess(Scene& scene) noexcept;

    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent Get(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TransformComponent& transform);
    void MarkModified(SceneEntity entity) noexcept;

    void ForEach(ConstTransformVisitor visitor, void* context = nullptr) const;
    void ForEachMutable(MutableTransformVisitor visitor, void* context = nullptr);

private:
    Scene& scene_;
};

} // namespace kb::scene
