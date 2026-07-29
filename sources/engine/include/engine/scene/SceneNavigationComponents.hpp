#pragma once

#include "engine/scene/Navigation.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

#define KB_DECLARE_SCENE_NAV_COMPONENT(Name, Type) \
class Scene##Name##ComponentQueries { \
public: \
    explicit Scene##Name##ComponentQueries(const Scene& scene) noexcept; \
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept; \
    [[nodiscard]] const Type* TryGet(SceneEntity entity) const noexcept; \
private: \
    const Scene& scene_; \
}; \
class Scene##Name##Components { \
public: \
    explicit Scene##Name##Components(Scene& scene) noexcept; \
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept; \
    [[nodiscard]] const Type* TryGet(SceneEntity entity) const noexcept; \
    [[nodiscard]] Type* TryGet(SceneEntity entity) noexcept; \
    void Set(SceneEntity entity, const Type& component); \
    void Remove(SceneEntity entity) noexcept; \
    void MarkModified(SceneEntity entity) noexcept; \
private: \
    Scene& scene_; \
};

KB_DECLARE_SCENE_NAV_COMPONENT(NavAgent, NavAgent)
KB_DECLARE_SCENE_NAV_COMPONENT(NavObstacle, NavObstacle)

#undef KB_DECLARE_SCENE_NAV_COMPONENT

} // namespace kb::scene
