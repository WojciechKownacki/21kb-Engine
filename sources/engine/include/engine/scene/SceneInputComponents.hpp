#pragma once

#include "engine/scene/InputComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneInputComponentQueries {
public:
    explicit SceneInputComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const InputComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneInputComponents {
public:
    explicit SceneInputComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const InputComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] InputComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const InputComponent& input);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
