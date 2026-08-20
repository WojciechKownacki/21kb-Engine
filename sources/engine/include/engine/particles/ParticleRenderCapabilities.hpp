#pragma once

#include <cstdint>

namespace kb::particles {

enum class ParticleRenderCapabilityStatus : std::uint8_t {
    Success,
    InvalidConsumer,
    ConsumerConflict,
    StaleFixedStep,
    GpuCatchupOverflow,
};

// Published by the renderer as the complete reason why visual GPU simulation
// can or cannot be selected for a scene. The value is stable until the next
// capability epoch; a backend must not change a live instance from it.
enum class ParticleGpuVisualAvailability : std::uint8_t {
    RendererUnavailable,
    ComputeUnsupported,
    ShaderUnavailable,
    ResourceUnavailable,
    DeviceFault,
    GpuCatchupOverflow,
    Ready,
};

enum class ParticleRenderOutputCapability : std::uint32_t {
    None = 0U,
    Billboard = 1U << 0U,
    StretchedBillboard = 1U << 1U,
    PointSprite = 1U << 2U,
    Mesh = 1U << 3U,
    Trail = 1U << 4U,
    Ribbon = 1U << 5U,
    Beam = 1U << 6U,
};

[[nodiscard]] constexpr ParticleRenderOutputCapability operator|(
    ParticleRenderOutputCapability lhs, ParticleRenderOutputCapability rhs) noexcept {
    return static_cast<ParticleRenderOutputCapability>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool HasParticleRenderOutputCapability(
    ParticleRenderOutputCapability value, ParticleRenderOutputCapability flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

struct ParticleRenderCapabilities {
    std::uint64_t capabilityEpoch = 0U;
    std::uint64_t lastConsumedFixedStep = 0U;
    ParticleRenderOutputCapability outputs = ParticleRenderOutputCapability::None;
    bool gpuDrawing = false;
    bool instancing = false;
    bool softParticles = false;
    bool subtractiveBlend = false;
    ParticleGpuVisualAvailability gpuVisualAvailability =
        ParticleGpuVisualAvailability::RendererUnavailable;
    std::uint32_t maxGpuVisualParticles = 0U;
    std::uint64_t maxGpuResourceBytes = 0U;
};

struct ParticleRenderCapabilityResult {
    ParticleRenderCapabilityStatus status = ParticleRenderCapabilityStatus::InvalidConsumer;

    [[nodiscard]] constexpr bool Succeeded() const noexcept {
        return status == ParticleRenderCapabilityStatus::Success;
    }
};

} // namespace kb::particles
