#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/AIBrainComponent.hpp"

namespace kb::scene {

class Scene;

class SceneAIBrainComponentQueries {
public:
    explicit SceneAIBrainComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AIBrainComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneAIBrainComponents {
public:
    explicit SceneAIBrainComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AIBrainComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AIBrainComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool Set(SceneEntity entity, const AIBrainComponent& aiBrain);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
