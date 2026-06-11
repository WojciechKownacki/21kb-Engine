#pragma once

#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneAudioSourceComponentQueries {
public:
    explicit SceneAudioSourceComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AudioSourceComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneAudioSourceComponents {
public:
    explicit SceneAudioSourceComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AudioSourceComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AudioSourceComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AudioSourceComponent& audioSource);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
