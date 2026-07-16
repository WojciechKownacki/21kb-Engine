#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "kb/render/resources/BuiltInParticleQuadMesh.hpp"

#include <algorithm>
#include <span>
#include <unordered_set>
#include <vector>

namespace kb::render {
namespace {

// Slot stride sized generously above kb::scene's own kMaxParticlesPerInstance (2048) rather
// than sharing that constant across the kb_engine/kb_render boundary; the base lives on the
// class (SceneParticleRenderSynchronizer::kSyntheticProxyIdBase) since LIB-144 so the
// visibility-feedback publisher can recognize synthetic particle proxies.
constexpr std::uint64_t kProxyIdSlotStride = 4096ULL;

[[nodiscard]] std::uint64_t ParticleProxyId(std::uint64_t instanceId, std::uint32_t slot) noexcept {
    return SceneParticleRenderSynchronizer::kSyntheticProxyIdBase + (instanceId * kProxyIdSlotStride) + static_cast<std::uint64_t>(slot);
}

[[nodiscard]] std::array<float, 16> FlattenModel(const kb::math::Mat4& model) noexcept {
    return std::array<float, 16>{
        model.columns[0].x, model.columns[0].y, model.columns[0].z, model.columns[0].w,
        model.columns[1].x, model.columns[1].y, model.columns[1].z, model.columns[1].w,
        model.columns[2].x, model.columns[2].y, model.columns[2].z, model.columns[2].w,
        model.columns[3].x, model.columns[3].y, model.columns[3].z, model.columns[3].w,
    };
}

} // namespace

void SceneParticleRenderSynchronizer::Sync(const kb::scene::Scene& scene, RenderScene& renderScene, std::uint32_t targetViewportId) {
    const CameraRenderProxyDesc* camera = renderScene.FindPrimaryCameraProxy(targetViewportId);
    const kb::math::Vec3 cameraPosition = camera != nullptr
        ? kb::math::Vec3{ camera->position[0], camera->position[1], camera->position[2] }
        : kb::math::Vec3{};
    const kb::math::Quat cameraRotation = camera != nullptr
        ? kb::math::Quat{ camera->rotation[0], camera->rotation[1], camera->rotation[2], camera->rotation[3] }
        : kb::math::Quat{};
    const kb::math::Vec3 cameraUp = kb::math::Rotate(cameraRotation, kb::math::Vec3{ 0.0F, 1.0F, 0.0F });

    const kb::scene::SceneParticleSystemQueries particles = scene.Particles();
    const std::vector<std::uint64_t> liveInstanceIds = particles.LiveInstanceIds();
    const std::unordered_set<std::uint64_t> liveSet{ liveInstanceIds.begin(), liveInstanceIds.end() };

    // Instances released since last frame: remove every proxy slot they left behind, then
    // stop tracking them.
    for (auto trackedIterator = lastFrameParticleCounts_.begin(); trackedIterator != lastFrameParticleCounts_.end();) {
        if (liveSet.find(trackedIterator->first) != liveSet.end()) {
            ++trackedIterator;
            continue;
        }
        for (std::uint32_t slot = 0U; slot < trackedIterator->second; ++slot) {
            static_cast<void>(renderScene.RemoveMesh(ParticleProxyId(trackedIterator->first, slot)));
        }
        trackedIterator = lastFrameParticleCounts_.erase(trackedIterator);
    }

    const kb::assets::AssetId quadMeshAssetId = BuiltInParticleQuadMeshAssetId();
    for (const std::uint64_t instanceId : liveInstanceIds) {
        const std::uint64_t materialAssetId = particles.ResolvedMaterialAsset(instanceId);
        const std::span<const kb::scene::ParticleState> liveParticles = particles.Particles(instanceId);
        const auto currentCount = static_cast<std::uint32_t>(liveParticles.size());
        const auto trackedIterator = lastFrameParticleCounts_.find(instanceId);
        const std::uint32_t previousCount = trackedIterator == lastFrameParticleCounts_.end() ? 0U : trackedIterator->second;

        // Re-resolved every frame (not cached) so a hot-reloaded .kbvfx file's size/color
        // curves take effect immediately - mirrors SceneParticleSystemService's own
        // every-Advance()-call re-resolution of the same asset.
        const kb::assets::AssetHandle<kb::scene::ParticleEffectAsset> effect =
            scene.Assets().Manager().Load<kb::scene::ParticleEffectAsset>(kb::assets::AssetId{ particles.EffectAsset(instanceId) });

        if (effect.IsLoaded() && materialAssetId != 0U) {
            for (std::uint32_t slot = 0U; slot < currentCount; ++slot) {
                const kb::scene::ParticleState& particle = liveParticles[slot];
                const float normalizedAge = particle.lifetime > 0.0F
                    ? kb::math::Clamp(particle.age / particle.lifetime, 0.0F, 1.0F)
                    : 0.0F;
                const float size = kb::math::Evaluate(effect->sizeOverLifetime, normalizedAge);
                const kb::math::Color color = kb::math::Evaluate(effect->colorOverLifetime, normalizedAge);

                const kb::math::Vec3 towardCamera = cameraPosition - particle.position;
                const kb::math::Quat billboardRotation = kb::math::LookRotation(towardCamera, cameraUp);
                const kb::math::Mat4 model = kb::math::FromTRS(particle.position, billboardRotation, kb::math::Vec3{ size, size, size });

                static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
                    .entityId = ParticleProxyId(instanceId, slot),
                    .meshAssetId = quadMeshAssetId.value,
                    .materialAssetId = materialAssetId,
                    .model = FlattenModel(model),
                    .color = { color.r, color.g, color.b, color.a },
                    .visible = true,
                    // Particle effects (smoke/sparks/etc) are not expected to cast believable
                    // shadows from a flat unlit-ish billboard - skip the extra shadow-pass
                    // submission cost by default.
                    .castsShadow = false,
                    .receivesShadow = true,
                }));
            }
        } else {
            // Effect/material transiently unresolvable this frame - treat as zero live
            // proxies (all slots below get cleaned up by the loop below) rather than
            // submitting geometry with guessed appearance data.
            for (std::uint32_t slot = 0U; slot < currentCount; ++slot) {
                static_cast<void>(renderScene.RemoveMesh(ParticleProxyId(instanceId, slot)));
            }
            lastFrameParticleCounts_[instanceId] = 0U;
            continue;
        }

        for (std::uint32_t slot = currentCount; slot < previousCount; ++slot) {
            static_cast<void>(renderScene.RemoveMesh(ParticleProxyId(instanceId, slot)));
        }
        lastFrameParticleCounts_[instanceId] = currentCount;
    }
}

} // namespace kb::render
