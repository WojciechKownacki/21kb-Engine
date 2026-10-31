#include "kb/render/particles/ParticleStripGeometryBuilder.hpp"

#include "kb/render/particles/ParticleRenderSpace.hpp"
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

struct StripColor {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

[[nodiscard]] StripColor Unpack(std::uint32_t packedColor, float intensity, float alphaScale = 1.0F) noexcept {
    return {static_cast<float>(packedColor & 0xFFU) / 255.0F * intensity,
        static_cast<float>((packedColor >> 8U) & 0xFFU) / 255.0F * intensity,
        static_cast<float>((packedColor >> 16U) & 0xFFU) / 255.0F * intensity,
        static_cast<float>((packedColor >> 24U) & 0xFFU) / 255.0F * alphaScale};
}

[[nodiscard]] ParticleStripVertex Vertex(kb::math::Vec3 position, StripColor color, float u, float v) noexcept {
    return {position.x, position.y, position.z, color.r, color.g, color.b, color.a, u, v};
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
    const SceneRenderCamera& renderCamera,
    kb::math::Vec3 renderOffset) noexcept {
    // Strips are built in particle space and offset into render space as their vertices are written.
    const SceneRenderCamera camera = ParticleSpaceCamera(renderCamera, renderOffset);
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

    if (trailHistoryRevision_ != snapshot.Revision() || trailHistorySceneId_ != snapshot.SceneId() ||
        trailHistoryBackendEpoch_ != snapshot.BackendEpoch()) {
        for (TrailHistory& history : trailHistories_) {
            if (history.sceneId == snapshot.SceneId() && history.particleId != 0U &&
                (history.backendEpoch != snapshot.BackendEpoch() ||
                    history.lastSeenRevision + 1U < snapshot.Revision())) history = {};
        }
        trailHistoryRevision_ = snapshot.Revision();
        trailHistorySceneId_ = snapshot.SceneId();
        trailHistoryBackendEpoch_ = snapshot.BackendEpoch();
    }

    // One camera-facing strip through `count` points (Unity's LineRenderer/TrailRenderer, Niagara's ribbon):
    // consecutive segments share their two vertices, each turned across the tangent at that point, so the
    // strip has no gaps or overlaps at the joints. U runs along the strip (0 at its oldest point, 1 at its
    // newest), V across it. A strip that does not fit the budget is dropped whole (count - 1 segments).
    const kb::math::Vec3 cameraForward = NormalizeOr({camera.view[2], camera.view[6], camera.view[10]}, {0.0F, 0.0F, 1.0F});
    const kb::math::Vec3 cameraRight = NormalizeOr({camera.view[0], camera.view[4], camera.view[8]}, {1.0F, 0.0F, 0.0F});
    const auto appendStrip = [&](std::size_t count, float width, const auto& positionAt, const auto& colorAt,
                                 const auto& uAt, std::uint32_t& dropped) noexcept {
        if (count < 2U) return;
        if (vertexScratch_.size() + 2U * count > kParticleStripVertexBudget ||
            indexScratch_.size() + 6U * (count - 1U) > kParticleStripIndexBudget) {
            dropped += static_cast<std::uint32_t>(count - 1U);
            return;
        }
        const std::uint16_t base = static_cast<std::uint16_t>(vertexScratch_.size());
        for (std::size_t point = 0U; point < count; ++point) {
            const kb::math::Vec3 position = positionAt(point);
            const kb::math::Vec3 previous = positionAt(point == 0U ? point : point - 1U);
            const kb::math::Vec3 next = positionAt(point + 1U < count ? point + 1U : point);
            const kb::math::Vec3 tangent = NormalizeOr(Subtract(next, previous), {0.0F, 1.0F, 0.0F});
            const kb::math::Vec3 side = Multiply(NormalizeOr(kb::math::Cross(cameraForward, tangent), cameraRight), width * 0.5F);
            const StripColor color = colorAt(point);
            const float u = uAt(point);
            vertexScratch_.push_back(Vertex(Add(Subtract(position, side), renderOffset), color, u, 0.0F));
            vertexScratch_.push_back(Vertex(Add(Add(position, side), renderOffset), color, u, 1.0F));
        }
        for (std::size_t segment = 0U; segment + 1U < count; ++segment) {
            const auto at = static_cast<std::uint16_t>(base + 2U * segment);
            indexScratch_.insert(indexScratch_.end(), {at, static_cast<std::uint16_t>(at + 1U), static_cast<std::uint16_t>(at + 3U),
                at, static_cast<std::uint16_t>(at + 3U), static_cast<std::uint16_t>(at + 2U)});
        }
    };

    std::uint32_t dropped = 0U;
    for (std::uint32_t emitterIndex = 0U; emitterIndex < emitters.size(); ++emitterIndex) {
        const auto& emitter = emitters[emitterIndex];
        if (emitter.output != kb::particles::ParticleRenderOutput::Trail &&
            emitter.output != kb::particles::ParticleRenderOutput::Ribbon &&
            emitter.output != kb::particles::ParticleRenderOutput::Beam) continue;
        const std::size_t firstIndex = indexScratch_.size();
        const float intensity = emitter.colorIntensity;
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
                    .emitterRecordIndex = emitterIndex, .origin = snapshot.Origin()};
                if (history.origin != snapshot.Origin()) {
                    // The simulation origin moved: keep the samples where they were in the world.
                    const kb::math::Vec3 shift = kb::math::RelativeTo(history.origin, snapshot.Origin());
                    for (kb::math::Vec3& sample : history.samples) sample = Add(sample, shift);
                    history.origin = snapshot.Origin();
                }
                history.lastSeenRevision = snapshot.Revision();
                history.emitterRecordIndex = emitterIndex;
                history.packedColor = particle.packedColor;
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
                // The trail takes its particle's colour and fades out toward the oldest sample.
                const float span = static_cast<float>(std::max<std::uint8_t>(1U, history.sampleCount - 1U));
                appendStrip(history.sampleCount, emitter.trailWidth,
                    [&](std::size_t sample) noexcept {
                        return history.samples[(history.firstSample + sample) % emitter.trailMaxSamplesPerParticle];
                    },
                    [&](std::size_t sample) noexcept {
                        return Unpack(history.packedColor, intensity, static_cast<float>(sample) / span);
                    },
                    [&](std::size_t sample) noexcept { return static_cast<float>(sample) / span; }, dropped);
            }
        } else if (emitter.output == kb::particles::ParticleRenderOutput::Ribbon) {
            for (std::uint32_t local = 0U; local < emitter.particleCount; ++local) particleOrderScratch_.push_back(emitter.firstParticle + local);
            const auto begin = particleOrderScratch_.end() - static_cast<std::ptrdiff_t>(emitter.particleCount);
            std::sort(begin, particleOrderScratch_.end(), [&](std::uint32_t lhs, std::uint32_t rhs) {
                const auto& a = particles[lhs]; const auto& b = particles[rhs];
                return a.ribbonGroup != b.ribbonGroup ? a.ribbonGroup < b.ribbonGroup : a.spawnOrdinal < b.spawnOrdinal;
            });
            const auto at = [&](std::size_t index) noexcept -> const kb::particles::ParticleRenderRecord& {
                return particles[*(begin + static_cast<std::ptrdiff_t>(index))];
            };
            std::uint32_t segments = 0U;
            for (std::size_t groupBegin = 0U; groupBegin < emitter.particleCount;) {
                std::size_t groupEnd = groupBegin + 1U;
                while (groupEnd < emitter.particleCount && at(groupEnd).ribbonGroup == at(groupBegin).ribbonGroup) ++groupEnd;
                const float span = static_cast<float>(std::max<std::size_t>(1U, groupEnd - groupBegin - 1U));
                // One strip per unbroken run of the group (a dead particle breaks it when asked to).
                for (std::size_t runBegin = groupBegin; runBegin < groupEnd;) {
                    std::size_t runEnd = runBegin + 1U;
                    while (runEnd < groupEnd &&
                        !(emitter.ribbonBreakOnDeath && at(runEnd).spawnOrdinal != at(runEnd - 1U).spawnOrdinal + 1U)) ++runEnd;
                    const std::size_t wanted = runEnd - runBegin - 1U;
                    const std::size_t allowed = std::min<std::size_t>(wanted, emitter.ribbonMaxSegments - segments);
                    dropped += static_cast<std::uint32_t>(wanted - allowed);
                    appendStrip(allowed + 1U, emitter.ribbonWidth,
                        [&](std::size_t point) noexcept { return at(runBegin + point).position; },
                        [&](std::size_t point) noexcept { return Unpack(at(runBegin + point).packedColor, intensity); },
                        [&](std::size_t point) noexcept { return static_cast<float>(runBegin + point - groupBegin) / span; },
                        dropped);
                    segments += static_cast<std::uint32_t>(allowed);
                    runBegin = runEnd;
                }
                groupBegin = groupEnd;
            }
        } else {
            const std::uint32_t segments = std::max<std::uint32_t>(1U, emitter.beamSegments);
            // The noise moves the beam across itself (the beam's up where it has one, otherwise world X),
            // never along it, so a vertical beam bends like a horizontal one; both ends stay where the
            // beam starts and hits.
            const kb::math::Vec3 axis = NormalizeOr(Subtract(emitter.beamEnd, emitter.outputOrigin), {0.0F, 1.0F, 0.0F});
            const kb::math::Vec3 worldUp{0.0F, 1.0F, 0.0F};
            const kb::math::Vec3 across = NormalizeOr(Subtract(worldUp, Multiply(axis, kb::math::Dot(worldUp, axis))),
                NormalizeOr(Subtract(kb::math::Vec3{1.0F, 0.0F, 0.0F}, Multiply(axis, axis.x)), {0.0F, 0.0F, 1.0F}));
            const StripColor white = Unpack(0xFFFFFFFFU, intensity);
            appendStrip(segments + 1U, emitter.beamWidth,
                [&](std::size_t point) noexcept {
                    const float t = static_cast<float>(point) / static_cast<float>(segments);
                    const kb::math::Vec3 linear = Add(emitter.outputOrigin, Multiply(Subtract(emitter.beamEnd, emitter.outputOrigin), t));
                    const float phase = static_cast<float>(snapshot.FixedStepIndex()) * emitter.beamNoiseFrequency + t * 31.0F;
                    const float envelope = std::sin(t * 3.14159265F);
                    return Add(linear, Multiply(across, std::sin(phase) * emitter.beamNoiseAmplitude * envelope));
                },
                [&](std::size_t) noexcept { return white; },
                [&](std::size_t point) noexcept { return static_cast<float>(point) / static_cast<float>(segments); }, dropped);
        }
        const std::uint32_t count = static_cast<std::uint32_t>(indexScratch_.size() - firstIndex);
        if (count != 0U) drawScratch_.push_back({.emitterRecordIndex = emitterIndex,
            .firstIndex = static_cast<std::uint32_t>(firstIndex), .indexCount = count,
            .textureAtlasAssetId = emitter.textureAtlasAssetId, .blend = emitter.blend,
            .depth = emitter.depth, .transparentDepthBucket = QuantizeTransparentViewDepth(ViewDepth(camera, emitter.outputOrigin))});
    }
    return {.status = dropped == 0U ? ParticleStripBuildStatus::Success : ParticleStripBuildStatus::CapacityExceeded,
        .vertices = vertexScratch_, .indices = indexScratch_, .draws = drawScratch_, .droppedSegmentCount = dropped};
}

} // namespace kb::render
