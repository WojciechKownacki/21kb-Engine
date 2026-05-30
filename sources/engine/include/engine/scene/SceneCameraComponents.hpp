#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneCameraComponentQueries {
public:
    explicit SceneCameraComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CameraComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneCameraComponents {
public:
    explicit SceneCameraComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CameraComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CameraComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CameraComponent& camera);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
