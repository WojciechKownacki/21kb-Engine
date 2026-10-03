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
// A trail emitter draws one stretched quad per trail segment of every slot; this bounds capacity x segments.
inline constexpr std::uint32_t kParticleGpuMaxTrailSegments = 1'048'576U;

struct ParticleGpuSpawn {
    kb::math::Vec3 position{};
    float birthTime = 0.0F;
    kb::math::Vec3 velocity{};
    float lifetime = 1.0F;
};

static_assert(sizeof(ParticleGpuSpawn) == 32U);
static_assert(std::is_trivially_copyable_v<ParticleGpuSpawn>);

// A world-space plane (normal . p = distance) that particles bounce off, like the CPU collision module.
struct ParticleGpuCollisionPlane {
    kb::math::Vec3 normal{ 0.0F, 1.0F, 0.0F };
    float distance = 0.0F;
    float restitution = 0.5F;
    float friction = 0.0F;
};

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
    // Collisions make the motion stateful: the renderer integrates each particle frame by frame instead of
    // evaluating the closed form. The plane is exact; scene depth collision bounces off whatever surface
    // the depth buffer of the frame shows (with the restitution and friction of the plane, or defaults).
    ParticleGpuCollisionPlane plane{};
    bool hasPlane = false;
    bool sceneDepthCollision = false;
    // Local-space emitters keep their birth records in the owner's frame; the world matrix sent with the
    // commands carries the particles along with the owner (acceleration is still a world-space vector).
    bool localSpace = false;
    // Mesh output: every particle is an instance of the mesh drawn through the ordinary mesh pipeline with the
    // material (so lighting and shadows apply); the instance uses the size curve as a uniform scale.
    std::uint64_t meshAssetId = 0U;
    std::uint64_t materialAssetId = 0U;
    bool castsShadow = false;
    bool receivesShadow = true;
    // Trail output: the path of a free-flying particle is known in closed form, so each slot draws
    // `trailSegments` camera-facing quads along its last trailSegments x trailSegmentSeconds of travel.
    std::uint32_t trailSegments = 0U;
    float trailSegmentSeconds = 0.0F;
    float trailWidth = 0.1F;

    [[nodiscard]] constexpr bool HasCollision() const noexcept { return hasPlane || sceneDepthCollision; }
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
    // Column-major world matrix of the owner, sent every step for local-space emitters.
    bool hasWorldMatrix = false;
    std::array<float, 16> worldMatrix{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
    std::vector<ParticleGpuSpawn> spawns;
};

} // namespace kb::particles
