#include "renderer/RendererTemporalJitter.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] float Halton(std::uint64_t index, std::uint32_t base) noexcept {
    float result = 0.0F;
    float fraction = 1.0F / static_cast<float>(base);
    while (index > 0U) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

} // namespace

std::array<float, 2> RendererTemporalJitter::Compute(std::uint64_t frameIndex, RenderExtent extent, bool enabled) noexcept {
    if (!enabled || !extent.IsValid()) {
        return {0.0F, 0.0F};
    }
    const std::uint64_t sequenceIndex = (frameIndex % 8ULL) + 1ULL;
    return {
        (Halton(sequenceIndex, 2U) - 0.5F) / static_cast<float>(std::max(1U, extent.width)),
        (Halton(sequenceIndex, 3U) - 0.5F) / static_cast<float>(std::max(1U, extent.height)),
    };
}

void RendererTemporalJitter::Apply(SceneRenderCamera& camera, std::array<float, 2> jitter, RenderExtent extent) noexcept {
    camera.temporalProjectionOffset = {};
    camera.cullingGuardBand = {};
    if (camera.projection[3] == 0.0F && camera.projection[7] == 0.0F &&
        camera.projection[11] != 0.0F && camera.projection[15] == 0.0F &&
        (jitter[0] != 0.0F || jitter[1] != 0.0F)) {
        camera.temporalProjectionOffset = {jitter[0] * 2.0F, jitter[1] * 2.0F};
        const float wScale = std::abs(camera.projection[11]);
        camera.cullingGuardBand = {
            std::max(std::abs(jitter[0]) * 2.0F, extent.IsValid() ? 1.0F / extent.width : 0.0F) / wScale,
            std::max(std::abs(jitter[1]) * 2.0F, extent.IsValid() ? 1.0F / extent.height : 0.0F) / wScale,
        };
    }
    camera.projection[8] += jitter[0] * 2.0F;
    camera.projection[9] += jitter[1] * 2.0F;
}

} // namespace kb::render
