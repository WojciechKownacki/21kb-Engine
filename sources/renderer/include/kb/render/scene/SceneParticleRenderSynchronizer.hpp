#pragma once

#include "kb/render/scene/RenderScene.hpp"
#include "engine/particles/ParticleRenderCapabilities.hpp"
#include "engine/particles/ParticleRenderSnapshot.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::render {

// Retains the latest complete engine-owned snapshot in renderer state. It never
// creates scene entities or mesh proxies, and therefore remains view-independent.
// Emitters with particle lights get point light proxies (ids from kParticleLightIdBase up),
// one per lit particle, replaced every sync.
inline constexpr std::uint64_t kParticleLightIdBase = 0xFFFF'0000'0000'0000ULL;

// The point lights of the lit particles of `snapshot` (none for a null snapshot): upserted into `renderScene`,
// their ids (sorted) left in `lightIds`, and the lights of the previous `lightIds` no longer lit removed.
void SyncParticleLights(const kb::particles::ParticleRenderSnapshot* snapshot, RenderScene& renderScene,
    std::vector<std::uint64_t>& lightIds);

class SceneParticleRenderSynchronizer final {
public:
    SceneParticleRenderSynchronizer();
    ~SceneParticleRenderSynchronizer();
    [[nodiscard]] bool Prepare(kb::scene::Scene& scene) noexcept;
    void Sync(const kb::scene::Scene& scene, RenderScene& renderScene);
    void Acknowledge(const kb::scene::Scene& scene, std::uint64_t fixedStepIndex);
    void ReleaseScene(const kb::scene::Scene& scene, RenderScene* renderScene = nullptr) noexcept;
    void Clear() noexcept;
    void SetGpuVisualAvailability(kb::particles::ParticleGpuVisualAvailability availability) noexcept;

    [[nodiscard]] std::uint64_t ConsumerId() const noexcept { return consumerId_; }

private:
    struct TrackedScene {
        kb::scene::Scene* scene = nullptr;
        std::uint64_t lastAcknowledgedFixedStep = 0U;
        std::vector<std::uint64_t> lightIds;
    };

    std::uint64_t consumerId_ = 0U;
    std::uint64_t capabilityEpoch_ = 1U;
    kb::particles::ParticleGpuVisualAvailability gpuVisualAvailability_ =
        kb::particles::ParticleGpuVisualAvailability::RendererUnavailable;
    std::unordered_map<std::uint64_t, TrackedScene> scenes_;
};

} // namespace kb::render
