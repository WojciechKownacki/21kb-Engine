#pragma once

#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::scene {
class Scene;
}

namespace kb::render {

// Retains the latest complete engine-owned snapshot in renderer state. It never
// creates scene entities or mesh proxies, and therefore remains view-independent.
class SceneParticleRenderSynchronizer final {
public:
    SceneParticleRenderSynchronizer();
    ~SceneParticleRenderSynchronizer();
    void Sync(const kb::scene::Scene& scene, RenderScene& renderScene);
    void Acknowledge(const kb::scene::Scene& scene, std::uint64_t fixedStepIndex);
    void ReleaseScene(const kb::scene::Scene& scene, RenderScene* renderScene = nullptr) noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::uint64_t ConsumerId() const noexcept { return consumerId_; }

private:
    std::uint64_t consumerId_ = 0U;
    std::unordered_map<std::uint64_t, kb::scene::Scene*> scenes_;
};

} // namespace kb::render
