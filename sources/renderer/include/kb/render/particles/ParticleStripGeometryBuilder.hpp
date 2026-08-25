#pragma once

#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

inline constexpr std::uint32_t kParticleStripVertexBudget = 65'536U;
inline constexpr std::uint32_t kParticleStripIndexBudget = 98'304U;
inline constexpr std::uint32_t kParticleTrailHistoryBudget = 16'384U;

struct ParticleStripVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
};

static_assert(sizeof(ParticleStripVertex) == 24U);

struct ParticleStripDraw {
    std::uint32_t emitterRecordIndex = 0U;
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
    kb::particles::ParticleRenderBlendMode blend = kb::particles::ParticleRenderBlendMode::Alpha;
    kb::particles::ParticleRenderDepthMode depth = kb::particles::ParticleRenderDepthMode::ReadOnly;
    std::uint16_t transparentDepthBucket = 0U;
};

enum class ParticleStripBuildStatus : std::uint8_t { Success, InvalidSnapshot, CapacityExceeded };

struct ParticleStripBuildResult {
    ParticleStripBuildStatus status = ParticleStripBuildStatus::InvalidSnapshot;
    std::span<const ParticleStripVertex> vertices;
    std::span<const std::uint16_t> indices;
    std::span<const ParticleStripDraw> draws;
    std::uint32_t droppedSegmentCount = 0U;
    [[nodiscard]] constexpr bool Succeeded() const noexcept { return status == ParticleStripBuildStatus::Success; }
    [[nodiscard]] constexpr bool Usable() const noexcept {
        return status == ParticleStripBuildStatus::Success || status == ParticleStripBuildStatus::CapacityExceeded;
    }
};

class ParticleStripGeometryBuilder final {
public:
    void Warmup();
    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllScenes() noexcept;
    [[nodiscard]] ParticleStripBuildResult Build(
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera) noexcept;

private:
    struct TrailHistory {
        std::uint64_t sceneId = 0U;
        std::uint64_t backendEpoch = 0U;
        std::uint64_t particleId = 0U;
        std::uint64_t lastSeenRevision = 0U;
        std::uint64_t lastSampleStep = 0U;
        std::uint32_t emitterRecordIndex = 0U;
        std::array<kb::math::Vec3, kb::scene::kParticleEffectMaxTrailSamplesPerParticle> samples{};
        std::uint8_t firstSample = 0U;
        std::uint8_t sampleCount = 0U;
    };

    std::vector<TrailHistory> trailHistories_;
    std::vector<std::uint32_t> particleOrderScratch_;
    std::vector<ParticleStripVertex> vertexScratch_;
    std::vector<std::uint16_t> indexScratch_;
    std::vector<ParticleStripDraw> drawScratch_;
    std::uint64_t trailHistoryRevision_ = 0U;
};

} // namespace kb::render
