#pragma once

#include <cstdint>
#include <memory>

namespace kb::scene {

class SceneAssets;
class SceneComponentQueries;
class SceneComponents;
class SceneEntities;
class SceneEntityQueries;
class SceneHierarchyAccess;
class SceneHierarchyQueries;
class SceneHistory;
class ScenePrefabs;
class SceneRuntime;
class SceneRuntimeQueries;
class SceneAccess;
class SceneState;
class SceneTransformQueries;
class SceneTransforms;

class Scene {
public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] SceneEntities Entities() noexcept;
    [[nodiscard]] SceneEntityQueries Entities() const noexcept;
    [[nodiscard]] SceneTransforms Transforms() noexcept;
    [[nodiscard]] SceneTransformQueries Transforms() const noexcept;
    [[nodiscard]] SceneComponents Components() noexcept;
    [[nodiscard]] SceneComponentQueries Components() const noexcept;
    [[nodiscard]] SceneHierarchyAccess Hierarchy() noexcept;
    [[nodiscard]] SceneHierarchyQueries Hierarchy() const noexcept;
    [[nodiscard]] SceneHistory History() noexcept;
    [[nodiscard]] SceneAssets Assets() noexcept;
    [[nodiscard]] SceneAssets Assets() const noexcept;
    [[nodiscard]] ScenePrefabs Prefabs() noexcept;
    [[nodiscard]] ScenePrefabs Prefabs() const noexcept;
    [[nodiscard]] SceneRuntime Runtime() noexcept;
    [[nodiscard]] SceneRuntimeQueries Runtime() const noexcept;
    [[nodiscard]] std::uint64_t Id() const noexcept;

private:
    friend class SceneAccess;

    std::unique_ptr<SceneState> state_;
    std::uint64_t id_ = 0;
};

} // namespace kb::scene
