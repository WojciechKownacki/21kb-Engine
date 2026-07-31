#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/StreamFocusComponent.hpp"

namespace kb::scene {

class Scene;

class SceneStreamFocusComponentQueries {
public:
    explicit SceneStreamFocusComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const StreamFocusComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneStreamFocusComponents {
public:
    explicit SceneStreamFocusComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const StreamFocusComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] StreamFocusComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const StreamFocusComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
