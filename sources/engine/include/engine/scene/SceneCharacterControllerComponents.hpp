#pragma once

#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneCharacterControllerComponentQueries {
public:
    explicit SceneCharacterControllerComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CharacterControllerComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneCharacterControllerComponents {
public:
    explicit SceneCharacterControllerComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CharacterControllerComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CharacterControllerComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CharacterControllerComponent& characterController);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
