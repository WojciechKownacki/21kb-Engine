#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace kb::render {

inline constexpr float kTransparentDepthBucketsPerMeter = 16.0F;

// Mesh instances and particle records use the same camera-space depth quantization before
// their draw commands enter the shared transparent queue. Larger keys are farther away.
[[nodiscard]] inline std::uint16_t QuantizeTransparentViewDepth(float viewDepth) noexcept {
    if (!std::isfinite(viewDepth)) return 0U;
    return static_cast<std::uint16_t>(std::clamp(
        std::abs(viewDepth) * kTransparentDepthBucketsPerMeter, 0.0F, 65'535.0F));
}

} // namespace kb::render
