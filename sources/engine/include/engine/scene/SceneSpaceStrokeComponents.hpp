#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"

namespace kb::scene {

class Scene;
using SpaceStrokeVisitor = void (*)(SceneEntity entity, const SpaceStrokeComponent& component, void* context);

class SceneSpaceStrokeComponentQueries {
public:
    explicit SceneSpaceStrokeComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SpaceStrokeComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(SpaceStrokeVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneSpaceStrokeComponents {
public:
    explicit SceneSpaceStrokeComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SpaceStrokeComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SpaceStrokeComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SpaceStrokeComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
