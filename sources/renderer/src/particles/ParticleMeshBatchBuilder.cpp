#include "kb/render/particles/ParticleMeshBatchBuilder.hpp"

#include "engine/math/EngineMath.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] std::array<float, 16> FlattenModel(const kb::math::Mat4& model) noexcept {
    return { model.columns[0].x, model.columns[0].y, model.columns[0].z, model.columns[0].w,
        model.columns[1].x, model.columns[1].y, model.columns[1].z, model.columns[1].w,
        model.columns[2].x, model.columns[2].y, model.columns[2].z, model.columns[2].w,
        model.columns[3].x, model.columns[3].y, model.columns[3].z, model.columns[3].w };
}

[[nodiscard]] kb::math::Quat UnpackEmitterBasis(
    const kb::particles::ParticleRenderEmitterRecord& emitter) noexcept {
    return kb::math::Normalize(kb::math::Quat{
        static_cast<float>(emitter.localBasisQuaternionSnorm[0]) / 32'767.0F,
        static_cast<float>(emitter.localBasisQuaternionSnorm[1]) / 32'767.0F,
        static_cast<float>(emitter.localBasisQuaternionSnorm[2]) / 32'767.0F,
        static_cast<float>(emitter.localBasisQuaternionSnorm[3]) / 32'767.0F,
    });
}

// The CPU particle sim tracks only a single scalar spin (rotationRadians, the same value used to
// rotate a billboard's quad corners) - there is no full per-particle 3D orientation. Mesh output
// composes that spin, around the emitter's own local Z axis, on top of the emitter's authored
// local/owner orientation (localBasisQuaternionSnorm), rather than leaving mesh particles
// unrotated. A future stage adding real per-particle 3D orientation would need a new snapshot
// field; this is a documented scope boundary of the current particle data model, not an oversight.
[[nodiscard]] kb::math::Quat SpinAroundZ(float radians) noexcept {
    const float half = radians * 0.5F;
    return kb::math::Quat{0.0F, 0.0F, std::sin(half), std::cos(half)};
}

[[nodiscard]] std::array<float, 4> UnpackColor(std::uint32_t packedColor) noexcept {
    const auto channel = [&](unsigned shift) noexcept {
        return static_cast<float>((packedColor >> shift) & 0xFFU) / 255.0F;
    };
    return {channel(0U), channel(8U), channel(16U), channel(24U)};
}

} // namespace

void ParticleMeshBatchBuilder::Warmup(std::uint32_t particleCapacity) {
    instances_.reserve(particleCapacity);
    batches_.reserve(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
}

void ParticleMeshBatchBuilder::Build(const kb::particles::ParticleRenderSnapshot& snapshot, const SceneRenderCamera* camera,
    const RenderResourceRegistry* resources, const SceneRenderResourceMap* resourceMap) noexcept {
    instances_.clear();
    batches_.clear();
    if (snapshot.IsTombstone()) return;

    const auto emitters = snapshot.Emitters();
    const auto particles = snapshot.Particles();

    // Pass 1: total instance count, so instances_ never reallocates while batches_ holds spans
    // into it (a mid-build reallocation would leave every earlier batch's span dangling).
    std::size_t totalInstances = 0U;
    for (const auto& emitter : emitters) {
        if (emitter.output != kb::particles::ParticleRenderOutput::Mesh) continue;
        if (emitter.firstParticle > particles.size() ||
            emitter.particleCount > particles.size() - emitter.firstParticle) continue;
        totalInstances += emitter.particleCount;
    }
    if (totalInstances == 0U) return;
    if (instances_.capacity() < totalInstances) instances_.reserve(totalInstances);

    // Pass 2: populate instances_ (capacity already sufficient) and one batch per Mesh emitter.
    for (const auto& emitter : emitters) {
        if (emitter.output != kb::particles::ParticleRenderOutput::Mesh) continue;
        if (emitter.firstParticle > particles.size() ||
            emitter.particleCount > particles.size() - emitter.firstParticle) continue;
        if (emitter.particleCount == 0U) continue;

        const kb::math::Quat basis = UnpackEmitterBasis(emitter);
        const bool castsShadow = kb::particles::HasParticleRenderEmitterFlag(
            emitter.flags, kb::particles::ParticleRenderEmitterFlag::CastsShadow);
        const bool receivesShadow = kb::particles::HasParticleRenderEmitterFlag(
            emitter.flags, kb::particles::ParticleRenderEmitterFlag::ReceivesShadow);

        // The draw order of a translucent emitter: indices into its particles, sorted like the billboard batcher does.
        orderScratch_.resize(emitter.particleCount);
        for (std::uint32_t local = 0U; local < emitter.particleCount; ++local) orderScratch_[local] = local;
        const RenderMaterialResource* material = resources != nullptr && resourceMap != nullptr
            ? resources->FindMaterial(resourceMap->ResolveMaterial(emitter.materialAssetId)) : nullptr;
        const bool translucent = material != nullptr && material->alphaMode == RenderMaterialAlphaMode::Blend;
        if (camera != nullptr && translucent && emitter.sort != kb::particles::ParticleRenderSortMode::None) {
            const std::array<float, 4> cameraPosition = SceneLightingPacker::CameraPosition(camera);
            const auto key = [&](std::uint32_t local) noexcept {
                const auto& particle = particles[emitter.firstParticle + local];
                switch (emitter.sort) {
                case kb::particles::ParticleRenderSortMode::BackToFront:
                case kb::particles::ParticleRenderSortMode::FrontToBack:
                    return camera->view[2] * particle.position.x + camera->view[6] * particle.position.y +
                        camera->view[10] * particle.position.z + camera->view[14];
                case kb::particles::ParticleRenderSortMode::Distance: {
                    const float dx = particle.position.x - cameraPosition[0];
                    const float dy = particle.position.y - cameraPosition[1];
                    const float dz = particle.position.z - cameraPosition[2];
                    return dx * dx + dy * dy + dz * dz;
                }
                case kb::particles::ParticleRenderSortMode::Age: return static_cast<float>(particle.normalizedAgeUnorm);
                case kb::particles::ParticleRenderSortMode::None: break;
                }
                return 0.0F;
            };
            const bool descending = emitter.sort != kb::particles::ParticleRenderSortMode::FrontToBack;
            std::sort(orderScratch_.begin(), orderScratch_.end(), [&](std::uint32_t lhs, std::uint32_t rhs) noexcept {
                const float lhsKey = key(lhs);
                const float rhsKey = key(rhs);
                if (lhsKey == rhsKey) return particles[emitter.firstParticle + lhs].particleId < particles[emitter.firstParticle + rhs].particleId;
                return descending ? lhsKey > rhsKey : lhsKey < rhsKey;
            });
        }
        const std::size_t firstInstance = instances_.size();
        for (const std::uint32_t local : orderScratch_) {
            const auto& particle = particles[emitter.firstParticle + local];
            const kb::math::Quat orientation = basis * SpinAroundZ(particle.rotationRadians);
            const kb::math::Vec3 scale{particle.size, particle.size, particle.size};
            SceneRenderMeshInstance instance{};
            instance.entityId = particle.particleId;
            instance.meshAssetId = emitter.meshAssetId;
            instance.materialAssetId = emitter.materialAssetId;
            instance.model = FlattenModel(kb::math::FromTRS(particle.position, orientation, scale));
            instance.color = UnpackColor(particle.packedColor);
            instance.castsShadow = castsShadow;
            instance.receivesShadow = receivesShadow;
            instance.lodBias = emitter.meshLodLevel;
            instances_.push_back(instance);
        }

        batches_.push_back(SceneMeshBatch{
            .meshAssetId = emitter.meshAssetId,
            .materialAssetId = emitter.materialAssetId,
            .sourceDrawGroupIndex = 0U,
            .instances = std::span<const SceneRenderMeshInstance>{
                instances_.data() + firstInstance, emitter.particleCount},
        });
    }
}

} // namespace kb::render
