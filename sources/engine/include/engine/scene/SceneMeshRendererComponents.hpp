#pragma once

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneMeshRendererComponentQueries {
public:
    explicit SceneMeshRendererComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const MeshRendererComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneMeshRendererComponents {
public:
    explicit SceneMeshRendererComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const MeshRendererComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] MeshRendererComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const MeshRendererComponent& renderer);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
