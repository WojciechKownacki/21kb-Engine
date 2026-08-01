#pragma once

#include "engine/scene/SceneHistoryRibbonComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneHistoryRibbonComponentStore {
public:
    SceneHistoryRibbonComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const HistoryRibbonComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] HistoryRibbonComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(HistoryRibbonVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const HistoryRibbonComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
