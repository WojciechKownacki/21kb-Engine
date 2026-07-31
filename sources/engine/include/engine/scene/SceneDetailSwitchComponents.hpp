#pragma once

#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneDetailSwitchComponentQueries {
public:
    explicit SceneDetailSwitchComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneDetailSwitchComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneDetailSwitchComponents {
public:
    explicit SceneDetailSwitchComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneDetailSwitchComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneDetailSwitchComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SceneDetailSwitchComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
