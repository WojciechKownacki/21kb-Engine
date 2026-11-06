#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/RiverComponent.hpp"

namespace kb::scene {

class Scene;
using RiverVisitor = void (*)(SceneEntity entity, const RiverComponent& component, void* context);

class SceneRiverComponentQueries {
public:
    explicit SceneRiverComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RiverComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(RiverVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneRiverComponents {
public:
    explicit SceneRiverComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RiverComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] RiverComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const RiverComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
