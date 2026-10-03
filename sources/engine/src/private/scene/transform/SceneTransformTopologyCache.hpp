#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace kb::scene {

class SceneState;

// Derived execution order only. Parent/child ownership remains in SceneState.
// Locations index the existing levels so streaming leaf removal is O(1).
class SceneTransformTopologyCache {
public:
    void Refresh(const SceneState& state);
    void Added(const SceneState& state, std::span<const SceneEntity> entities, std::uint64_t previousVersion);
    void Removed(const SceneState& state, SceneEntity entity, std::uint64_t previousVersion) noexcept;
    void Moved(const SceneState& state, SceneEntity entity, std::uint64_t previousVersion);
    void LeafRemovalHandled(std::uint64_t before, std::uint64_t after) noexcept;
    [[nodiscard]] std::uint64_t RemovalSafetyEpoch(std::uint64_t currentRemovalVersion) noexcept;
    void MetadataChanged(std::uint64_t previousVersion, std::uint64_t currentVersion) noexcept;

    [[nodiscard]] const std::vector<std::vector<SceneEntity>>& Levels() const noexcept { return levels_; }
    [[nodiscard]] std::uint64_t Version() const noexcept { return version_; }
    [[nodiscard]] std::uint64_t BuildCount() const noexcept { return buildCount_; }

private:
    struct Location {
        SceneEntity entity{};
        std::size_t level = 0U;
        std::size_t offset = 0U;
    };
    [[nodiscard]] const Location* Find(SceneEntity entity) const noexcept;
    void Store(Location location);
    void Append(SceneEntity entity, std::size_t level);
    void Erase(SceneEntity entity) noexcept;

    std::vector<std::vector<SceneEntity>> levels_;
    std::vector<Location> dense_;
    std::unordered_map<SceneEntity::IdType, Location> sparse_;
    std::uint64_t version_ = 0U;
    std::uint64_t buildCount_ = 0U;
    std::uint64_t nativeRemovalVersion_ = 0U;
    std::uint64_t removalSafetyEpoch_ = 0U;
};

} // namespace kb::scene
