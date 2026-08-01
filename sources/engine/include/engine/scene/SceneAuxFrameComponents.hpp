#pragma once

#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

using AuxFrameVisitor = void (*)(SceneEntity entity, const AuxFrameComponent& component, void* context);

class SceneAuxFrameComponentQueries {
public:
    explicit SceneAuxFrameComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AuxFrameComponent* TryGet(SceneEntity entity) const noexcept;
    // Executes directly over the canonical ECS archetypes.  Runtime consumers
    // must use this instead of maintaining a second component index.
    void ForEach(AuxFrameVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneAuxFrameComponents {
public:
    explicit SceneAuxFrameComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AuxFrameComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AuxFrameComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AuxFrameComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
