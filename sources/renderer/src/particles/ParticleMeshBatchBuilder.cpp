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

// The orientation whose Y axis is `forward` (unit length), X horizontal-ish (from the world up) and Z = X x Y.
[[nodiscard]] kb::math::Quat FollowVelocity(kb::math::Vec3 forward) noexcept {
    const kb::math::Vec3 helper = std::fabs(forward.y) < 0.99F ? kb::math::Vec3{0.0F, 1.0F, 0.0F} : kb::math::Vec3{0.0F, 0.0F, 1.0F};
    const kb::math::Vec3 x = kb::math::Normalize(kb::math::Cross(helper, forward));
    const kb::math::Vec3 y = forward;
    const kb::math::Vec3 z = kb::math::Cross(x, y);
    // Rotation matrix with the columns x, y, z to a quaternion.
    const float trace = x.x + y.y + z.z;
    kb::math::Quat q{};
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25F * s};
    } else if (x.x > y.y && x.x > z.z) {
        const float s = std::sqrt(1.0F + x.x - y.y - z.z) * 2.0F;
        q = {0.25F * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const float s = std::sqrt(1.0F + y.y - x.x - z.z) * 2.0F;
        q = {(y.x + x.y) / s, 0.25F * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const float s = std::sqrt(1.0F + z.z - x.x - y.y) * 2.0F;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25F * s, (x.y - y.x) / s};
    }
    return kb::math::Normalize(q);
}

[[nodiscard]] kb::math::Quat SpinAroundY(float radians) noexcept {
    const float half = radians * 0.5F;
    return kb::math::Quat{0.0F, std::sin(half), 0.0F, std::cos(half)};
}

[[nodiscard]] kb::math::Quat SpinAroundX(float radians) noexcept {
    const float half = radians * 0.5F;
    return kb::math::Quat{std::sin(half), 0.0F, 0.0F, std::cos(half)};
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
    const RenderResourceRegistry* resources, const SceneRenderResourceMap* resourceMap, kb::math::Vec3 renderOffset) noexcept {
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
                const kb::math::Vec3 position = particle.position + renderOffset;
                switch (emitter.sort) {
                case kb::particles::ParticleRenderSortMode::BackToFront:
                case kb::particles::ParticleRenderSortMode::FrontToBack:
                    return camera->view[2] * position.x + camera->view[6] * position.y +
                        camera->view[10] * position.z + camera->view[14];
                case kb::particles::ParticleRenderSortMode::Distance: {
                    const float dx = position.x - cameraPosition[0];
                    const float dy = position.y - cameraPosition[1];
                    const float dz = position.z - cameraPosition[2];
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
            // Euler turn about X, then Y, then Z (z is rotationRadians, the spin a billboard uses too).
            // A mesh that follows its velocity is turned from a frame whose Y axis is the velocity instead of the
            // emitter's basis (the basis stays for a particle that is standing still).
            const float speedSquared = kb::math::Dot(particle.velocity, particle.velocity);
            const kb::math::Quat frame = emitter.alignment == kb::particles::ParticleRenderAlignment::Velocity && speedSquared > 1.0e-10F
                ? FollowVelocity(particle.velocity * (1.0F / std::sqrt(speedSquared))) : basis;
            const kb::math::Quat orientation = frame * (SpinAroundZ(particle.rotationRadians) *
                SpinAroundY(kb::particles::UnpackParticleAngle(particle.rotationYSnorm)) *
                SpinAroundX(kb::particles::UnpackParticleAngle(particle.rotationXSnorm)));
            const kb::math::Vec3 scale{particle.size, particle.size, particle.size};
            SceneRenderMeshInstance instance{};
            instance.entityId = particle.particleId;
            instance.meshAssetId = emitter.meshAssetId;
            instance.materialAssetId = emitter.materialAssetId;
            instance.model = FlattenModel(kb::math::FromTRS(particle.position + renderOffset, orientation, scale));
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
