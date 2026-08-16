#pragma once

#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kParticleGpuInstancesPerDraw = 16'384U;
inline constexpr std::size_t kParticleGpuMaxBatches =
    kb::particles::kParticleRenderSnapshotMaxEmitterRecords +
    (kb::scene::kParticleEffectMaxCpuParticlesPerScene + kParticleGpuInstancesPerDraw - 1U) /
        kParticleGpuInstancesPerDraw;
inline constexpr float kParticleAlignmentEpsilon = 1.0e-5F;
inline constexpr float kParticleSoftFadeDistanceMeters = 0.10F;

enum class ParticleRenderBatchStatus : std::uint8_t {
    Success,
    InvalidSnapshot,
    CapacityExceeded,
};

enum class ParticleRenderBatchDropReason : std::uint8_t {
    None,
    InstanceCapacity,
    UnsupportedOutput,
};

struct ParticleGpuInstance {
    std::array<float, 4> positionSize{};
    std::array<float, 4> previousPositionRotation{};
    std::array<float, 4> velocityStretch{};
    std::array<float, 4> color{};
    std::array<float, 4> frameAgeIdentity{};
};

static_assert(sizeof(ParticleGpuInstance) == 80U);

struct ParticleRenderBatch {
    std::uint32_t emitterRecordIndex = 0U;
    std::uint32_t firstInstance = 0U;
    std::uint32_t instanceCount = 0U;
    std::uint64_t materialAssetId = 0U;
    std::uint64_t textureAtlasAssetId = 0U;
    kb::particles::ParticleRenderOutput output = kb::particles::ParticleRenderOutput::Billboard;
    kb::particles::ParticleRenderBlendMode blend = kb::particles::ParticleRenderBlendMode::Alpha;
    kb::particles::ParticleRenderDepthMode depth = kb::particles::ParticleRenderDepthMode::ReadOnly;
    kb::particles::ParticleRenderSortMode sort = kb::particles::ParticleRenderSortMode::None;
    std::uint16_t transparentDepthBucket = 0U;
    ParticleRenderBatchDropReason droppedReason = ParticleRenderBatchDropReason::None;
};

struct ParticleRenderBatchBuildResult {
    ParticleRenderBatchStatus status = ParticleRenderBatchStatus::InvalidSnapshot;
    std::span<const ParticleRenderBatch> batches;
    std::span<const ParticleGpuInstance> instances;
    std::span<const std::uint32_t> unsupportedEmitterRecordIndices;
    std::uint32_t droppedParticleCount = 0U;
    std::uint32_t unsupportedEmitterCount = 0U;

    [[nodiscard]] constexpr bool Succeeded() const noexcept {
        return status == ParticleRenderBatchStatus::Success;
    }
};

struct ParticleAlignmentBasis {
    std::array<float, 3> right{};
    std::array<float, 3> up{};
    std::array<float, 3> forward{};
};

enum class TransparentDrawSource : std::uint8_t { Mesh, Particle };
struct TransparentDrawOrderEntry {
    TransparentDrawSource source = TransparentDrawSource::Mesh;
    bool unsorted = false;
    std::uint16_t depthBucket = 0U;
    std::uint32_t sourceIndex = 0U;
    std::uint64_t stableTie = 0U;
};

class ParticleRenderBatcher final {
public:
    void Warmup(std::uint32_t particleCapacity);
    [[nodiscard]] ParticleRenderBatchBuildResult Build(
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera) noexcept;

    [[nodiscard]] std::uint32_t Capacity() const noexcept;

private:
    std::vector<std::uint32_t> orderScratch_;
    std::vector<std::uint8_t> emitterHandledScratch_;
    std::vector<std::uint32_t> unsupportedEmitterScratch_;
    std::vector<ParticleGpuInstance> instanceScratch_;
    std::vector<ParticleRenderBatch> batchScratch_;
    std::uint32_t capacity_ = 0U;
};

[[nodiscard]] float ParticleSoftFade(float sceneDepth, float particleDepth) noexcept;
[[nodiscard]] std::array<float, 2> RotateParticleCorner(
    std::array<float, 2> corner, float radians) noexcept;
[[nodiscard]] ParticleAlignmentBasis ResolveParticleAlignmentBasis(
    const kb::particles::ParticleRenderEmitterRecord& emitter,
    const kb::particles::ParticleRenderRecord& particle,
    const SceneRenderCamera& camera) noexcept;
[[nodiscard]] std::uint64_t ParticleBlendState(
    kb::particles::ParticleRenderBlendMode blend,
    kb::particles::ParticleRenderDepthMode depth) noexcept;
void SortTransparentDrawOrder(std::span<TransparentDrawOrderEntry> entries) noexcept;

} // namespace kb::render
