#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/Renderer.hpp"

#include <cstdint>
#include <vector>

namespace kb::game {

// Preserve proxy changes queued before Update, which consumes the runtime queue.
class RuntimeSceneFrameSync {
public:
    void BeforeUpdate(const kb::scene::Scene& scene) {
        preUpdateChanges_.clear();
        if (initialized_ && scene.Runtime().RenderProxyUpdateRevision() != synchronizedProxyRevision_) {
            for (const auto entity : scene.Runtime().RenderProxyUpdateEntities()) {
                preUpdateChanges_.push_back(entity.Id());
            }
        }
    }

    [[nodiscard]] bool Submit(kb::scene::Scene& scene, kb::render::Renderer& renderer) {
        const std::uint64_t topology = scene.Runtime().RenderTopologyVersion();
        const std::uint64_t proxyRevision = scene.Runtime().RenderProxyUpdateRevision();
        if (!renderer.SubmitRuntimeScene(scene, {
                .fullSync = !initialized_ || topology != synchronizedTopology_,
                .dirtySceneEntityIds = preUpdateChanges_,
            })) {
            return false;
        }
        initialized_ = true;
        synchronizedTopology_ = topology;
        synchronizedProxyRevision_ = proxyRevision;
        return true;
    }

    void Reset() noexcept {
        initialized_ = false;
        synchronizedTopology_ = 0U;
        synchronizedProxyRevision_ = 0U;
        preUpdateChanges_.clear();
    }

private:
    bool initialized_ = false;
    std::uint64_t synchronizedTopology_ = 0U;
    std::uint64_t synchronizedProxyRevision_ = 0U;
    std::vector<std::uint64_t> preUpdateChanges_;
};

} // namespace kb::game
