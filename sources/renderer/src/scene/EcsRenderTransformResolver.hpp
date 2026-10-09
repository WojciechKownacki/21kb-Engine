#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace kb::scene {
class Scene;
}

namespace kb::render {

class EcsRenderTransformResolver {
public:
    using TransformCache = std::unordered_map<std::uint64_t, kb::scene::TransformComponent>;
    using ResolvingSet = std::unordered_set<std::uint64_t>;

    EcsRenderTransformResolver(const kb::scene::Scene& scene, TransformCache& cache, ResolvingSet& resolving) noexcept;

    [[nodiscard]] kb::scene::TransformComponent Resolve(kb::scene::SceneEntity entity);
    // The entity's double-precision world translation: the scene's composed one, or, for a row written after the last
    // transform sync, composed here from its ancestors' (docs/large_worlds.md).
    [[nodiscard]] kb::math::DVec3 ResolveWorldTranslation(kb::scene::SceneEntity entity);
    [[nodiscard]] const kb::scene::Scene& Scene() const noexcept { return scene_; }

private:
    [[nodiscard]] static kb::scene::TransformComponent Identity() noexcept;
    [[nodiscard]] static kb::scene::TransformComponent Compose(
        const kb::scene::TransformComponent& parent,
        const kb::scene::TransformComponent& local) noexcept;

    const kb::scene::Scene& scene_;
    TransformCache& cache_;
    ResolvingSet& resolving_;
};

} // namespace kb::render
