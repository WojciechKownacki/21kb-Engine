#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneAudioListenerComponentQueries {
public:
    explicit SceneAudioListenerComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AudioListenerComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneAudioListenerComponents {
public:
    explicit SceneAudioListenerComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AudioListenerComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AudioListenerComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AudioListenerComponent& audioListener);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
