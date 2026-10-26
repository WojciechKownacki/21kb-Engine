#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace kb::particles {

// Compiled ranges have at least one frame and fit within the 256 by 256 atlas limit.
// Reduce in double precision before narrowing so long-lived or very fast CPU effects do not overflow.
[[nodiscard]] inline std::uint16_t ParticleFlipbookFrame(float age, float framesPerSecond,
    std::uint32_t startFrame, std::uint32_t frameCount, bool looping) noexcept {
    if (frameCount <= 1U || !(framesPerSecond > 0.0F) || !(age > 0.0F) ||
        !std::isfinite(age) || !std::isfinite(framesPerSecond)) return static_cast<std::uint16_t>(startFrame);
    const double product = static_cast<double>(age) * static_cast<double>(framesPerSecond);
    const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
    const float ticks = static_cast<float>(std::min(product, maxFloat));
    const double elapsed = std::floor(product <= maxFloat ? static_cast<double>(ticks) : product);
    const double offset = looping ? std::fmod(elapsed, static_cast<double>(frameCount)) :
        std::min(elapsed, static_cast<double>(frameCount - 1U));
    return static_cast<std::uint16_t>(startFrame + static_cast<std::uint32_t>(offset));
}

} // namespace kb::particles
