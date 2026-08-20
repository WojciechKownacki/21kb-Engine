#include "kb/render/particles/ParticleStripGeometryBuilder.hpp"

#include "kb/render/scene/TransparentDepthKey.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] kb::math::Vec3 Subtract(kb::math::Vec3 lhs, kb::math::Vec3 rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] kb::math::Vec3 Add(kb::math::Vec3 lhs, kb::math::Vec3 rhs) noexcept {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

[[nodiscard]] kb::math::Vec3 Multiply(kb::math::Vec3 value, float scalar) noexcept {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] kb::math::Vec3 NormalizeOr(kb::math::Vec3 value, kb::math::Vec3 fallback) noexcept {
    const float lengthSquared = kb::math::Dot(value, value);
    return lengthSquared > 0.0000001F ? Multiply(value, 1.0F / std::sqrt(lengthSquared)) : fallback;
}

[[nodiscard]] float ViewDepth(const SceneRenderCamera& camera, kb::math::Vec3 position) noexcept {
    return camera.view[2] * position.x + camera.view[6] * position.y + camera.view[10] * position.z + camera.view[14];
}

[[nodiscard]] ParticleStripVertex Vertex(kb::math::Vec3 position, std::uint32_t packedColor) noexcept {
    return {position.x, position.y, position.z,
        static_cast<float>(packedColor & 0xFFU) / 255.0F,
        static_cast<float>((packedColor >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>((packedColor >> 16U) & 0xFFU) / 255.0F};
}

} // namespace

void ParticleStripGeometryBuilder::Warmup() {
    trailHistories_.resize(kParticleTrailHistoryBudget);
    particleOrderScratch_.reserve(kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    vertexScratch_.reserve(kParticleStripVertexBudget);
    indexScratch_.reserve(kParticleStripIndexBudget);
    drawScratch_.reserve(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
}

void ParticleStripGeometryBuilder::ReleaseScene(std::uint64_t sceneId) noexcept {
    for (TrailHistory& history : trailHistories_) {
        if (history.sceneId == sceneId) history = {};
    }
    trailHistoryRevision_ = 0U;
}

void ParticleStripGeometryBuilder::ReleaseAllScenes() noexcept {
    std::fill(trailHistories_.begin(), trailHistories_.end(), TrailHistory{});
    trailHistoryRevision_ = 0U;
}

ParticleStripBuildResult ParticleStripGeometryBuilder::Build(
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera) noexcept {
    vertexScratch_.clear();
    indexScratch_.clear();
    drawScratch_.clear();
    particleOrderScratch_.clear();
    if (snapshot.IsTombstone()) return {.status = ParticleStripBuildStatus::Success};
    const auto emitters = snapshot.Emitters();
    const auto particles = snapshot.Particles();
    for (const auto& emitter : emitters) {
        if (emitter.firstParticle > particles.size() || emitter.particleCount > particles.size() - emitter.firstParticle) {
            return {.status = ParticleStripBuildStatus::InvalidSnapshot};
        }
    }

    if (trailHistoryRevision_ != snapshot.Revision()) {
        for (TrailHistory& history : trailHistories_) {
            if (history.sceneId == snapshot.SceneId() && history.particleId != 0U &&
                (history.backendEpoch != snapshot.BackendEpoch() ||
                    history.lastSeenRevision + 1U < snapshot.Revision())) history = {};
        }
        trailHistoryRevision_ = snapshot.Revision();
    }

    const auto appendSegment = [&](kb::math::Vec3 start, kb::math::Vec3 end, float width,
                                   std::uint32_t color, std::uint32_t& dropped) noexcept {
        if (vertexScratch_.size() + 4U > kParticleStripVertexBudget ||
            indexScratch_.size() + 6U > kParticleStripIndexBudget) { ++dropped; return; }
        const kb::math::Vec3 direction = NormalizeOr(Subtract(end, start), {0.0F, 1.0F, 0.0F});
        const kb::math::Vec3 cameraForward = NormalizeOr({camera.view[2], camera.view[6], camera.view[10]}, {0.0F, 0.0F, 1.0F});
        const kb::math::Vec3 cameraRight = NormalizeOr({camera.view[0], camera.view[4], camera.view[8]}, {1.0F, 0.0F, 0.0F});
        const kb::math::Vec3 side = Multiply(NormalizeOr(kb::math::Cross(cameraForward, direction), cameraRight), width * 0.5F);
        const std::uint16_t base = static_cast<std::uint16_t>(vertexScratch_.size());
        vertexScratch_.push_back(Vertex(Subtract(start, side), color));
        vertexScratch_.push_back(Vertex(Add(start, side), color));
        vertexScratch_.push_back(Vertex(Add(end, side), color));
        vertexScratch_.push_back(Vertex(Subtract(end, side), color));
        indexScratch_.insert(indexScratch_.end(), {base, static_cast<std::uint16_t>(base + 1U), static_cast<std::uint16_t>(base + 2U),
            base, static_cast<std::uint16_t>(base + 2U), static_cast<std::uint16_t>(base + 3U)});
    };

    std::uint32_t dropped = 0U;
    for (std::uint32_t emitterIndex = 0U; emitterIndex < emitters.size(); ++emitterIndex) {
        const auto& emitter = emitters[emitterIndex];
        if (emitter.output != kb::particles::ParticleRenderOutput::Trail &&
            emitter.output != kb::particles::ParticleRenderOutput::Ribbon &&
            emitter.output != kb::particles::ParticleRenderOutput::Beam) continue;
        const std::size_t firstIndex = indexScratch_.size();
        if (emitter.output == kb::particles::ParticleRenderOutput::Trail) {
            const std::uint64_t cadence = std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(std::ceil(
                emitter.trailSampleIntervalSeconds * static_cast<float>(kb::scene::kParticleEffectFixedStepsPerSecond))));
            for (std::uint32_t local = 0U; local < emitter.particleCount; ++local) {
                const auto& particle = particles[emitter.firstParticle + local];
                std::size_t slot = static_cast<std::size_t>(particle.particleId % trailHistories_.size());
                std::size_t probes = 0U;
                while (trailHistories_[slot].particleId != 0U &&
                    (trailHistories_[slot].sceneId != snapshot.SceneId() ||
                        trailHistories_[slot].backendEpoch != snapshot.BackendEpoch() ||
                        trailHistories_[slot].particleId != particle.particleId)) {
                    slot = (slot + 1U) % trailHistories_.size();
                    if (++probes == trailHistories_.size()) break;
                }
                if (probes == trailHistories_.size()) { ++dropped; continue; }
                TrailHistory& history = trailHistories_[slot];
                if (history.particleId == 0U) history = {.sceneId = snapshot.SceneId(),
                    .backendEpoch = snapshot.BackendEpoch(), .particleId = particle.particleId,
                    .emitterRecordIndex = emitterIndex};
                history.lastSeenRevision = snapshot.Revision();
                history.emitterRecordIndex = emitterIndex;
                if (history.sampleCount == 0U || snapshot.FixedStepIndex() >= history.lastSampleStep + cadence) {
                    if (history.sampleCount == emitter.trailMaxSamplesPerParticle) {
                        history.firstSample = static_cast<std::uint8_t>((history.firstSample + 1U) % emitter.trailMaxSamplesPerParticle);
                        --history.sampleCount;
                    }
                    const std::uint8_t write = static_cast<std::uint8_t>((history.firstSample + history.sampleCount) % emitter.trailMaxSamplesPerParticle);
                    const kb::math::Vec3 previous = history.sampleCount == 0U ? particle.position :
                        history.samples[(history.firstSample + history.sampleCount - 1U) % emitter.trailMaxSamplesPerParticle];
                    if (history.sampleCount == 0U || kb::math::Dot(Subtract(particle.position, previous), Subtract(particle.position, previous)) >=
                            emitter.trailMinimumDistance * emitter.trailMinimumDistance) {
                        history.samples[write] = particle.position;
                        ++history.sampleCount;
                        history.lastSampleStep = snapshot.FixedStepIndex();
                    }
                }
            }
            for (const TrailHistory& history : trailHistories_) {
                if (history.sceneId != snapshot.SceneId() || history.backendEpoch != snapshot.BackendEpoch() ||
                    history.lastSeenRevision != snapshot.Revision() ||
                    history.emitterRecordIndex != emitterIndex) continue;
                for (std::uint8_t sample = 1U; sample < history.sampleCount; ++sample) {
                    appendSegment(history.samples[(history.firstSample + sample - 1U) % emitter.trailMaxSamplesPerParticle],
                        history.samples[(history.firstSample + sample) % emitter.trailMaxSamplesPerParticle], emitter.trailWidth,
                        0xFFFFFFFFU, dropped);
                }
            }
        } else if (emitter.output == kb::particles::ParticleRenderOutput::Ribbon) {
            for (std::uint32_t local = 0U; local < emitter.particleCount; ++local) particleOrderScratch_.push_back(emitter.firstParticle + local);
            const auto begin = particleOrderScratch_.end() - static_cast<std::ptrdiff_t>(emitter.particleCount);
            std::sort(begin, particleOrderScratch_.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
                const auto& a = particles[lhs]; const auto& b = particles[rhs];
                return a.ribbonGroup != b.ribbonGroup ? a.ribbonGroup < b.ribbonGroup : a.spawnOrdinal < b.spawnOrdinal;
            });
            std::uint32_t segments = 0U;
            for (std::size_t index = 1U; index < emitter.particleCount; ++index) {
                const auto& previous = particles[*(begin + static_cast<std::ptrdiff_t>(index - 1U))];
                const auto& current = particles[*(begin + static_cast<std::ptrdiff_t>(index))];
                if (previous.ribbonGroup != current.ribbonGroup ||
                    (emitter.ribbonBreakOnDeath && current.spawnOrdinal != previous.spawnOrdinal + 1U)) continue;
                if (segments == emitter.ribbonMaxSegments) { ++dropped; continue; }
                appendSegment(previous.position, current.position, emitter.ribbonWidth, current.packedColor, dropped);
                ++segments;
            }
        } else {
            const std::uint32_t segments = emitter.beamSegments;
            for (std::uint32_t segment = 0U; segment < segments; ++segment) {
                const float a = static_cast<float>(segment) / static_cast<float>(segments);
                const float b = static_cast<float>(segment + 1U) / static_cast<float>(segments);
                const auto point = [&](float t) noexcept {
                    const kb::math::Vec3 linear = Add(emitter.outputOrigin, Multiply(Subtract(emitter.beamEnd, emitter.outputOrigin), t));
                    const float phase = static_cast<float>(snapshot.FixedStepIndex()) * emitter.beamNoiseFrequency + t * 31.0F;
                    return Add(linear, {0.0F, std::sin(phase) * emitter.beamNoiseAmplitude, 0.0F});
                };
                appendSegment(point(a), point(b), emitter.beamWidth, 0xFFFFFFFFU, dropped);
            }
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indexScratch_.size() - firstIndex);
        if (count != 0U) drawScratch_.push_back({.emitterRecordIndex = emitterIndex,
            .firstIndex = static_cast<std::uint32_t>(firstIndex), .indexCount = count, .blend = emitter.blend,
            .depth = emitter.depth, .transparentDepthBucket = QuantizeTransparentViewDepth(ViewDepth(camera, emitter.outputOrigin))});
    }
    return {.status = dropped == 0U ? ParticleStripBuildStatus::Success : ParticleStripBuildStatus::CapacityExceeded,
        .vertices = vertexScratch_, .indices = indexScratch_, .draws = drawScratch_, .droppedSegmentCount = dropped};
}

} // namespace kb::render
