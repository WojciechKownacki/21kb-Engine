#pragma once

#include "engine/math/EngineMath.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace kb::scene {

enum class ChainIkSolver : std::uint8_t {
    Fabrik,
    Ccd,
};

struct ChainIkSettings {
    ChainIkSolver solver = ChainIkSolver::Fabrik;
    // Solver passes before it gives up, and the distance from the target that counts as reached.
    std::uint32_t maxIterations = 16U;
    float tolerance = 0.001F;
};

inline constexpr std::uint32_t kChainIkMaxIterations = 1000U;

[[nodiscard]] constexpr bool IsChainIkSettingsValid(const ChainIkSettings& settings) noexcept {
    return (settings.solver == ChainIkSolver::Fabrik || settings.solver == ChainIkSolver::Ccd) &&
        settings.maxIterations >= 1U && settings.maxIterations <= kChainIkMaxIterations &&
        settings.tolerance > 0.0F && settings.tolerance <= 1.0e6F;
}

// The shortest turn taking direction `from` to `to`; the identity when either has no direction. Exact at
// every angle, so small corrections are not lost.
[[nodiscard]] kb::math::Quat ShortestArc(kb::math::Vec3 from, kb::math::Vec3 to) noexcept;

// A unit direction at right angles to `axis`, which must have a direction.
[[nodiscard]] kb::math::Vec3 AnyPerpendicular(kb::math::Vec3 axis) noexcept;

// Moves a chain of joints (root first, any number of them) so its last joint reaches `target`. The root stays
// put and every segment keeps its length; a target out of reach leaves the chain stretched straight toward it.
// With `pole` the joints between the ends bend toward it, about the root-to-effector axis.
// `segmentLengths` is scratch for joints.size() - 1 values, so a solve never allocates.
// Throws std::invalid_argument for fewer than two joints, too little scratch, a zero-length segment,
// non-finite input or invalid settings.
void SolveChainIk(
    std::span<kb::math::Vec3> joints, std::span<float> segmentLengths, kb::math::Vec3 target,
    std::optional<kb::math::Vec3> pole, const ChainIkSettings& settings);

} // namespace kb::scene
