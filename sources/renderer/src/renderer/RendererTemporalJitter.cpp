#include "renderer/RendererTemporalJitter.hpp"

#include <algorithm>

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

void RendererTemporalJitter::Apply(SceneRenderCamera& camera, std::array<float, 2> jitter) noexcept {
    camera.projection[8] += jitter[0] * 2.0F;
    camera.projection[9] += jitter[1] * 2.0F;
}

} // namespace kb::render
