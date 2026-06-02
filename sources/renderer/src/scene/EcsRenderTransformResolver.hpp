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
