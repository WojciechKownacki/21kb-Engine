#pragma once

#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;
using HistoryRibbonVisitor = void (*)(SceneEntity entity, const HistoryRibbonComponent& component, void* context);

class SceneHistoryRibbonComponentQueries {
public:
    explicit SceneHistoryRibbonComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const HistoryRibbonComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(HistoryRibbonVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneHistoryRibbonComponents {
public:
    explicit SceneHistoryRibbonComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const HistoryRibbonComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] HistoryRibbonComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const HistoryRibbonComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene
