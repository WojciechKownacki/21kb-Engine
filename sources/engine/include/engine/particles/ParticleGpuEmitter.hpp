#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/particles/ParticleRenderSnapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace kb::particles {

// GPU-simulated emitters keep no per-particle state on the CPU. At birth the simulation backend
// writes one 32-byte record per particle (world-space start position and velocity, birth time and
// lifetime); the renderer uploads the records into a ring buffer and a compute pass evaluates every
// particle in closed form (constant acceleration plus linear drag), so cost and capacity no longer
// scale with the CPU particle pipeline.
inline constexpr std::size_t kParticleGpuCurveSamples = 8U;
inline constexpr std::uint32_t kParticleGpuMaxCapacity = 1'048'576U;

struct ParticleGpuSpawn {
    kb::math::Vec3 position{};
    float birthTime = 0.0F;
    kb::math::Vec3 velocity{};
    float lifetime = 1.0F;
};

static_assert(sizeof(ParticleGpuSpawn) == 32U);
static_assert(std::is_trivially_copyable_v<ParticleGpuSpawn>);

// Appearance and motion constants of one GPU emitter. Colour and size over normalized age are
// sampled at kParticleGpuCurveSamples evenly spaced ages and interpolated linearly by the GPU.
struct ParticleGpuEmitterParams {
    kb::math::Vec3 acceleration{};
    float drag = 0.0F;
    std::array<std::array<float, 4U>, kParticleGpuCurveSamples> color{};
    std::array<float, kParticleGpuCurveSamples> size{};
    std::uint64_t textureAtlasAssetId = 0U;
    std::uint32_t capacity = 0U;
    ParticleRenderOutput output = ParticleRenderOutput::Billboard;
    ParticleRenderBlendMode blend = ParticleRenderBlendMode::Alpha;
    ParticleRenderDepthMode depth = ParticleRenderDepthMode::ReadOnly;
    ParticleRenderAlignment alignment = ParticleRenderAlignment::CameraFacing;
    bool softParticles = false;
    float stretchVelocityScale = 0.0F;
    float stretchMinimumLength = 1.0F;
};

struct ParticleGpuEmitterKey {
    std::uint64_t instanceId = 0U;
    std::uint64_t emitterId = 0U;

    [[nodiscard]] friend constexpr bool operator==(const ParticleGpuEmitterKey&, const ParticleGpuEmitterKey&) noexcept = default;
};

// One fixed step of one GPU emitter. `simTime` is the scene simulation time (seconds) at the end of
// the step; birth times in `spawns` are on the same clock. The renderer drains these exactly once.
struct ParticleGpuEmitterCommand {
    ParticleGpuEmitterKey key{};
    double simTime = 0.0;
    bool release = false;       // destroy the emitter's GPU state
    bool clear = false;         // drop all live particles (restart/stop with clear)
    bool hasParams = false;     // params below are valid (sent on creation and whenever they change)
    ParticleGpuEmitterParams params{};
    std::vector<ParticleGpuSpawn> spawns;
};

} // namespace kb::particles
