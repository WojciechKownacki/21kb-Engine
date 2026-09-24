#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

class SceneHierarchyRootsService {
public:
    SceneHierarchyRootsService() = delete;

    [[nodiscard]] static std::vector<SceneObject> RootObjects(Scene& scene);
    [[nodiscard]] static std::vector<SceneEntity> RootEntities(const Scene& scene);
    [[nodiscard]] static std::size_t RootCount(const Scene& scene) noexcept;
    [[nodiscard]] static SceneEntity RootAt(const Scene& scene, std::size_t index) noexcept;
    [[nodiscard]] static std::uint64_t RootAppendEpoch(const Scene& scene) noexcept;
};

} // namespace kb::scene
