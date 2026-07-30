#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;

struct ResolvedVisibility {
    bool visible = true;
    std::uint32_t mask = 0xFFFFFFFFU;
};

// Resolves the authored gate without caching. The hierarchy already guarantees
// acyclicity, and the walk is bounded by the live parent chain.
[[nodiscard]] ResolvedVisibility ResolveVisibility(const Scene& scene, SceneEntity entity) noexcept;

} // namespace kb::scene
