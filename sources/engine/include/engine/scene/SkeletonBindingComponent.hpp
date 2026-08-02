#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

// The binding owns only the authored selection of a shared skeleton asset.
// Hierarchy, bone map, and reference pose remain canonical data in that asset;
// evaluated poses and GPU palettes are transient motion-system cache data.
struct SkeletonBindingComponent {
    static constexpr std::string_view StableId = "kb21.motion.skeleton-binding";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t skeletonAssetId = 0U;
    std::uint64_t skeletonCompatibilitySignature = 0U;
};

[[nodiscard]] constexpr bool IsSkeletonBindingComponentValid(const SkeletonBindingComponent& value) noexcept {
    return value.skeletonAssetId != 0U && value.skeletonCompatibilitySignature != 0U;
}

} // namespace kb::scene
