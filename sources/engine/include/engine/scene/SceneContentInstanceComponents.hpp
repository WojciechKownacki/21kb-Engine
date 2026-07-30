#pragma once

#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneContentInstanceComponentQueries {
public:
    explicit SceneContentInstanceComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ContentInstanceComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneContentInstanceComponents {
public:
    explicit SceneContentInstanceComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ContentInstanceComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] ContentInstanceComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const ContentInstanceComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
