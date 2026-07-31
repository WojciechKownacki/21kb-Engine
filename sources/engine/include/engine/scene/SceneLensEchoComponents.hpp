#pragma once

#include "engine/scene/LensEchoComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;
using LensEchoVisitor = void (*)(SceneEntity entity, const LensEchoComponent& component, void* context);

class SceneLensEchoComponentQueries {
public:
    explicit SceneLensEchoComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const LensEchoComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(LensEchoVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneLensEchoComponents {
public:
    explicit SceneLensEchoComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const LensEchoComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] LensEchoComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const LensEchoComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
