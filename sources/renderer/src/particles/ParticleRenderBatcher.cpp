#include "kb/render/particles/ParticleRenderBatcher.hpp"

#include "kb/render/scene/TransparentDepthKey.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace kb::render {
namespace {

[[nodiscard]] bool SupportedOutput(kb::particles::ParticleRenderOutput output) noexcept {
    return output == kb::particles::ParticleRenderOutput::Billboard ||
        output == kb::particles::ParticleRenderOutput::StretchedBillboard ||
        output == kb::particles::ParticleRenderOutput::PointSprite;
}

[[nodiscard]] bool BatchCompatible(
    const kb::particles::ParticleRenderEmitterRecord& lhs,
    const kb::particles::ParticleRenderEmitterRecord& rhs) noexcept {
    return lhs.materialAssetId == rhs.materialAssetId &&
        lhs.textureAtlasAssetId == rhs.textureAtlasAssetId && lhs.output == rhs.output &&
        lhs.blend == rhs.blend && lhs.depth == rhs.depth && lhs.sort == rhs.sort &&
        lhs.alignment == rhs.alignment && lhs.flags == rhs.flags &&
        lhs.backendPolicy == rhs.backendPolicy &&
        lhs.flipbookColumnsEncoded == rhs.flipbookColumnsEncoded &&
        lhs.flipbookRowsEncoded == rhs.flipbookRowsEncoded &&
        lhs.localBasisQuaternionSnorm == rhs.localBasisQuaternionSnorm &&
        lhs.pointSpriteDiameter == rhs.pointSpriteDiameter;
}

[[nodiscard]] std::array<float, 3> CameraPosition(const SceneRenderCamera& camera) noexcept {
    const float tx = camera.view[12];
    const float ty = camera.view[13];
    const float tz = camera.view[14];
    return {
        -(camera.view[0] * tx + camera.view[1] * ty + camera.view[2] * tz),
        -(camera.view[4] * tx + camera.view[5] * ty + camera.view[6] * tz),
        -(camera.view[8] * tx + camera.view[9] * ty + camera.view[10] * tz),
    };
}

[[nodiscard]] float ViewDepth(
    const SceneRenderCamera& camera,
    const kb::particles::ParticleRenderRecord& particle) noexcept {
    return camera.view[2] * particle.position.x + camera.view[6] * particle.position.y +
        camera.view[10] * particle.position.z + camera.view[14];
}

[[nodiscard]] float DistanceSquared(
    const std::array<float, 3>& cameraPosition,
    const kb::particles::ParticleRenderRecord& particle) noexcept {
    const float dx = particle.position.x - cameraPosition[0];
    const float dy = particle.position.y - cameraPosition[1];
    const float dz = particle.position.z - cameraPosition[2];
    return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] ParticleGpuInstance Pack(const kb::particles::ParticleRenderRecord& source) noexcept {
    const auto color = [&](unsigned shift) noexcept {
        return static_cast<float>((source.packedColor >> shift) & 0xFFU) / 255.0F;
    };
    return {
        .positionSize = {source.position.x, source.position.y, source.position.z, source.size},
        .previousPositionRotation = {
            source.previousPosition.x, source.previousPosition.y, source.previousPosition.z, source.rotationRadians},
        .velocityStretch = {source.velocity.x, source.velocity.y, source.velocity.z, source.stretch},
        .color = {color(0U), color(8U), color(16U), color(24U)},
        .frameAgeIdentity = {
            static_cast<float>(source.frame),
            static_cast<float>(source.normalizedAgeUnorm) / 65'535.0F,
            std::bit_cast<float>(static_cast<std::uint32_t>(source.particleId)),
            std::bit_cast<float>(static_cast<std::uint32_t>(source.particleId >> 32U))},
    };
}

using Vec3 = std::array<float, 3>;

[[nodiscard]] float Dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

[[nodiscard]] Vec3 Cross(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs[1] * rhs[2] - lhs[2] * rhs[1], lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0]};
}

[[nodiscard]] Vec3 NormalizeOr(Vec3 value, Vec3 fallback) noexcept {
    const float lengthSquared = Dot(value, value);
    if (!(lengthSquared > kParticleAlignmentEpsilon * kParticleAlignmentEpsilon) ||
        !std::isfinite(lengthSquared)) return fallback;
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {value[0] * inverseLength, value[1] * inverseLength, value[2] * inverseLength};
}

[[nodiscard]] Vec3 Rotate(Vec3 value, std::array<float, 4> quaternion) noexcept {
    const Vec3 q{quaternion[0], quaternion[1], quaternion[2]};
    const Vec3 inner = Cross(q, value);
    const Vec3 nested = Cross(q, {inner[0] + quaternion[3] * value[0],
        inner[1] + quaternion[3] * value[1], inner[2] + quaternion[3] * value[2]});
    return {value[0] + 2.0F * nested[0], value[1] + 2.0F * nested[1], value[2] + 2.0F * nested[2]};
}

} // namespace

void ParticleRenderBatcher::Warmup(std::uint32_t particleCapacity) {
    capacity_ = particleCapacity;
    orderScratch_.reserve(particleCapacity);
    emitterHandledScratch_.reserve(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
    unsupportedEmitterScratch_.reserve(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
    instanceScratch_.reserve(particleCapacity);
    batchScratch_.reserve(kParticleGpuMaxBatches);
}

ParticleRenderBatchBuildResult ParticleRenderBatcher::Build(
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera) noexcept {
    orderScratch_.clear();
    instanceScratch_.clear();
    batchScratch_.clear();
    unsupportedEmitterScratch_.clear();
    emitterHandledScratch_.assign(snapshot.Emitters().size(), 0U);
    if (snapshot.IsTombstone()) {
        return {.status = ParticleRenderBatchStatus::Success};
    }

    const auto particles = snapshot.Particles();
    const auto emitters = snapshot.Emitters();
    if (particles.size() > capacity_) {
        return {.status = ParticleRenderBatchStatus::CapacityExceeded,
            .droppedParticleCount = static_cast<std::uint32_t>(particles.size() - capacity_)};
    }

    const std::array<float, 3> cameraPosition = CameraPosition(camera);
    std::uint32_t droppedParticleCount = 0U;
    std::uint32_t unsupportedEmitterCount = 0U;
    for (std::uint32_t emitterIndex = 0U; emitterIndex < emitters.size(); ++emitterIndex) {
        if (emitterHandledScratch_[emitterIndex] != 0U) continue;
        const auto& emitter = emitters[emitterIndex];
        emitterHandledScratch_[emitterIndex] = 1U;
        if (emitter.firstParticle > particles.size() ||
            emitter.particleCount > particles.size() - emitter.firstParticle) {
            return {.status = ParticleRenderBatchStatus::InvalidSnapshot};
        }
        if (emitter.output == kb::particles::ParticleRenderOutput::Mesh ||
            emitter.output == kb::particles::ParticleRenderOutput::Trail ||
            emitter.output == kb::particles::ParticleRenderOutput::Ribbon ||
            emitter.output == kb::particles::ParticleRenderOutput::Beam) {
            // Mesh uses the mesh-instancing path and strip outputs use renderer-owned dynamic geometry;
            // neither uses this quad/billboard instance format. They are not drops, so they must not
            // count against droppedParticleCount or unsupportedEmitterCount.
            continue;
        }
        if (!SupportedOutput(emitter.output)) {
            droppedParticleCount += emitter.particleCount;
            unsupportedEmitterScratch_.push_back(emitterIndex);
            ++unsupportedEmitterCount;
            continue;
        }

        const std::size_t orderBegin = orderScratch_.size();
        for (std::uint32_t candidateIndex = emitterIndex; candidateIndex < emitters.size(); ++candidateIndex) {
            if (emitterHandledScratch_[candidateIndex] != 0U && candidateIndex != emitterIndex) continue;
            const auto& candidate = emitters[candidateIndex];
            if (!BatchCompatible(emitter, candidate)) continue;
            if (candidate.firstParticle > particles.size() ||
                candidate.particleCount > particles.size() - candidate.firstParticle) {
                return {.status = ParticleRenderBatchStatus::InvalidSnapshot};
            }
            emitterHandledScratch_[candidateIndex] = 1U;
            for (std::uint32_t local = 0U; local < candidate.particleCount; ++local) {
                orderScratch_.push_back(candidate.firstParticle + local);
            }
        }
        const auto groupCount = static_cast<std::uint32_t>(orderScratch_.size() - orderBegin);
        const bool sortDisabled = emitter.blend == kb::particles::ParticleRenderBlendMode::Add ||
            emitter.sort == kb::particles::ParticleRenderSortMode::None;
        if (!sortDisabled) {
            const auto begin = orderScratch_.begin() + static_cast<std::ptrdiff_t>(orderBegin);
            const auto compare = [&](std::uint32_t lhsIndex, std::uint32_t rhsIndex) noexcept {
                const auto& lhs = particles[lhsIndex];
                const auto& rhs = particles[rhsIndex];
                float lhsKey = 0.0F;
                float rhsKey = 0.0F;
                bool descending = true;
                switch (emitter.sort) {
                case kb::particles::ParticleRenderSortMode::BackToFront:
                    lhsKey = ViewDepth(camera, lhs); rhsKey = ViewDepth(camera, rhs); break;
                case kb::particles::ParticleRenderSortMode::FrontToBack:
                    lhsKey = ViewDepth(camera, lhs); rhsKey = ViewDepth(camera, rhs); descending = false; break;
                case kb::particles::ParticleRenderSortMode::Distance:
                    lhsKey = DistanceSquared(cameraPosition, lhs); rhsKey = DistanceSquared(cameraPosition, rhs); break;
                case kb::particles::ParticleRenderSortMode::Age:
                    lhsKey = static_cast<float>(lhs.normalizedAgeUnorm);
                    rhsKey = static_cast<float>(rhs.normalizedAgeUnorm); break;
                case kb::particles::ParticleRenderSortMode::None:
                    break;
                }
                if (lhsKey == rhsKey) return lhs.particleId < rhs.particleId;
                return descending ? lhsKey > rhsKey : lhsKey < rhsKey;
            };
            std::sort(begin, orderScratch_.end(), compare);
        }

        std::uint32_t remaining = groupCount;
        std::size_t orderOffset = orderBegin;
        while (remaining != 0U) {
            const std::uint32_t drawCount = std::min(remaining, kParticleGpuInstancesPerDraw);
            const std::uint32_t firstInstance = static_cast<std::uint32_t>(instanceScratch_.size());
            for (std::uint32_t index = 0U; index < drawCount; ++index) {
                instanceScratch_.push_back(Pack(particles[orderScratch_[orderOffset + index]]));
            }
            const float batchViewDepth = drawCount == 0U ? 0.0F :
                ViewDepth(camera, particles[orderScratch_[orderOffset]]);
            batchScratch_.push_back(ParticleRenderBatch{
                .emitterRecordIndex = emitterIndex,
                .firstInstance = firstInstance,
                .instanceCount = drawCount,
                .materialAssetId = emitter.materialAssetId,
                .textureAtlasAssetId = emitter.textureAtlasAssetId,
                .output = emitter.output,
                .blend = emitter.blend,
                .depth = emitter.depth,
                .sort = emitter.sort,
                .transparentDepthBucket = QuantizeTransparentViewDepth(batchViewDepth),
            });
            orderOffset += drawCount;
            remaining -= drawCount;
        }
    }
    return {
        .status = ParticleRenderBatchStatus::Success,
        .batches = batchScratch_,
        .instances = instanceScratch_,
        .unsupportedEmitterRecordIndices = unsupportedEmitterScratch_,
        .droppedParticleCount = droppedParticleCount,
        .unsupportedEmitterCount = unsupportedEmitterCount,
    };
}

std::uint32_t ParticleRenderBatcher::Capacity() const noexcept { return capacity_; }

float ParticleSoftFade(float sceneDepth, float particleDepth) noexcept {
    return std::clamp((sceneDepth - particleDepth) / kParticleSoftFadeDistanceMeters, 0.0F, 1.0F);
}

std::array<float, 2> RotateParticleCorner(
    std::array<float, 2> corner, float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {corner[0] * cosine - corner[1] * sine,
        corner[0] * sine + corner[1] * cosine};
}

ParticleAlignmentBasis ResolveParticleAlignmentBasis(
    const kb::particles::ParticleRenderEmitterRecord& emitter,
    const kb::particles::ParticleRenderRecord& particle,
    const SceneRenderCamera& camera) noexcept {
    const Vec3 cameraRight = NormalizeOr({camera.view[0], camera.view[4], camera.view[8]}, {1.0F, 0.0F, 0.0F});
    const Vec3 cameraUp = NormalizeOr({camera.view[1], camera.view[5], camera.view[9]}, {0.0F, 1.0F, 0.0F});
    const Vec3 cameraForward = NormalizeOr({camera.view[2], camera.view[6], camera.view[10]}, {0.0F, 0.0F, 1.0F});
    ParticleAlignmentBasis result{cameraRight, cameraUp, cameraForward};
    if (emitter.alignment == kb::particles::ParticleRenderAlignment::Velocity) {
        const Vec3 velocity{particle.velocity.x, particle.velocity.y, particle.velocity.z};
        if (Dot(velocity, velocity) <= kParticleAlignmentEpsilon * kParticleAlignmentEpsilon) return result;
        result.up = NormalizeOr(velocity, cameraUp);
        result.right = NormalizeOr(Cross(cameraForward, result.up), cameraRight);
        result.up = NormalizeOr(Cross(result.right, cameraForward), cameraUp);
    } else if (emitter.alignment == kb::particles::ParticleRenderAlignment::WorldUp) {
        result.right = NormalizeOr(Cross(cameraForward, {0.0F, 1.0F, 0.0F}), cameraRight);
        result.up = NormalizeOr(Cross(result.right, cameraForward), cameraUp);
    } else if (emitter.alignment == kb::particles::ParticleRenderAlignment::Local) {
        std::array<float, 4> quaternion{};
        float lengthSquared = 0.0F;
        for (std::size_t index = 0U; index < quaternion.size(); ++index) {
            quaternion[index] = std::max(-1.0F,
                static_cast<float>(emitter.localBasisQuaternionSnorm[index]) / 32'767.0F);
            lengthSquared += quaternion[index] * quaternion[index];
        }
        if (std::isfinite(lengthSquared) && lengthSquared > 0.999F && lengthSquared < 1.001F) {
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            for (float& component : quaternion) component *= inverseLength;
            result.right = NormalizeOr(Rotate({1.0F, 0.0F, 0.0F}, quaternion), cameraRight);
            result.up = NormalizeOr(Rotate({0.0F, 1.0F, 0.0F}, quaternion), cameraUp);
            result.forward = NormalizeOr(Cross(result.right, result.up), cameraForward);
        }
    }
    return result;
}

std::uint64_t ParticleBlendState(
    kb::particles::ParticleRenderBlendMode blend,
    kb::particles::ParticleRenderDepthMode depth) noexcept {
    std::uint64_t state = BGFX_STATE_WRITE_RGB;
    if (blend != kb::particles::ParticleRenderBlendMode::Subtract) {
        state |= BGFX_STATE_WRITE_A;
    }
    const bool depthRead = blend == kb::particles::ParticleRenderBlendMode::Subtract ||
        depth == kb::particles::ParticleRenderDepthMode::ReadOnly ||
        depth == kb::particles::ParticleRenderDepthMode::ReadWrite;
    const bool depthWrite = blend != kb::particles::ParticleRenderBlendMode::Subtract &&
        (depth == kb::particles::ParticleRenderDepthMode::WriteOnly ||
         depth == kb::particles::ParticleRenderDepthMode::ReadWrite);
    if (depthRead) state |= BGFX_STATE_DEPTH_TEST_GEQUAL;
    if (depthWrite) state |= BGFX_STATE_WRITE_Z;
    switch (blend) {
    case kb::particles::ParticleRenderBlendMode::Opaque: break;
    case kb::particles::ParticleRenderBlendMode::Alpha: state |= BGFX_STATE_BLEND_ALPHA; break;
    case kb::particles::ParticleRenderBlendMode::Add: state |= BGFX_STATE_BLEND_ADD; break;
    case kb::particles::ParticleRenderBlendMode::Multiply: state |= BGFX_STATE_BLEND_MULTIPLY; break;
    case kb::particles::ParticleRenderBlendMode::Premultiplied:
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA); break;
    case kb::particles::ParticleRenderBlendMode::Subtract:
        state |= BGFX_STATE_BLEND_FUNC_SEPARATE(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE,
            BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
        state |= BGFX_STATE_BLEND_EQUATION_SEPARATE(
            BGFX_STATE_BLEND_EQUATION_REVSUB, BGFX_STATE_BLEND_EQUATION_ADD);
        break;
    }
    return state;
}

void SortTransparentDrawOrder(std::span<TransparentDrawOrderEntry> entries) noexcept {
    std::sort(entries.begin(), entries.end(),
        [](const TransparentDrawOrderEntry& lhs, const TransparentDrawOrderEntry& rhs) noexcept {
            if (lhs.unsorted != rhs.unsorted) return !lhs.unsorted;
            if (!lhs.unsorted && lhs.depthBucket != rhs.depthBucket) return lhs.depthBucket > rhs.depthBucket;
            if (lhs.stableTie != rhs.stableTie) return lhs.stableTie < rhs.stableTie;
            return lhs.source < rhs.source;
        });
}

} // namespace kb::render
