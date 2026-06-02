#pragma once

#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

namespace kb::scene {
class Scene;
}

namespace kb::render {

class SceneRenderExtractor {
public:
    void Reserve(const EcsRenderSceneSynchronizerReserveDesc& desc);
    void ExtractInto(const kb::scene::Scene& scene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, SceneRenderSnapshot& outSnapshot) const;
    [[nodiscard]] EcsRenderSceneSynchronizerStats SyncStats() const noexcept;

private:
    mutable RenderScene renderScene_;
    mutable EcsRenderSceneSynchronizer synchronizer_;
};

} // namespace kb::render
