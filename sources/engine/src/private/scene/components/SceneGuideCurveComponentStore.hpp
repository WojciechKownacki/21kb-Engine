#pragma once

#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneGuideCurveComponentStore {
public:
    SceneGuideCurveComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GuideCurveComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] GuideCurveComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const GuideCurveComponent& curve);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
