#pragma once

#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneNavigationComponentStore final {
public:
    explicit SceneNavigationComponentStore(kb::ecs::World& world) noexcept;
    [[nodiscard]] bool HasNavAgent(SceneEntity entity) const noexcept;
    [[nodiscard]] const NavAgent* TryGetNavAgent(SceneEntity entity) const noexcept;
    [[nodiscard]] NavAgent* TryGetNavAgent(SceneEntity entity) noexcept;
    void SetNavAgent(SceneEntity entity, const NavAgent& component);
    void RemoveNavAgent(SceneEntity entity) noexcept;
    void MarkNavAgentModified(SceneEntity entity) noexcept;
    [[nodiscard]] bool HasNavObstacle(SceneEntity entity) const noexcept;
    [[nodiscard]] const NavObstacle* TryGetNavObstacle(SceneEntity entity) const noexcept;
    [[nodiscard]] NavObstacle* TryGetNavObstacle(SceneEntity entity) noexcept;
    void SetNavObstacle(SceneEntity entity, const NavObstacle& component);
    void RemoveNavObstacle(SceneEntity entity) noexcept;
    void MarkNavObstacleModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
