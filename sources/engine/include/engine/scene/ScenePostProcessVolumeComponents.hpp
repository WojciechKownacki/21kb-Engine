#pragma once

#include "engine/scene/PostProcessVolumeComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class ScenePostProcessVolumeComponentQueries {
public:
    explicit ScenePostProcessVolumeComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const PostProcessVolumeComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class ScenePostProcessVolumeComponents {
public:
    explicit ScenePostProcessVolumeComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const PostProcessVolumeComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] PostProcessVolumeComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const PostProcessVolumeComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
