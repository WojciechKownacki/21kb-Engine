#pragma once

#include "engine/scene/CharacterController2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneCharacterController2DComponentQueries {
public:
    explicit SceneCharacterController2DComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CharacterController2DComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneCharacterController2DComponents {
public:
    explicit SceneCharacterController2DComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CharacterController2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CharacterController2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CharacterController2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
