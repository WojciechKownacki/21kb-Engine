#include "kb/render/scene/SceneRenderExtractor.hpp"

namespace kb::render {

void SceneRenderExtractor::Reserve(const EcsRenderSceneSynchronizerReserveDesc& desc) {
    synchronizer_.Reserve(desc);
}

void SceneRenderExtractor::ExtractInto(const kb::scene::Scene& scene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, SceneRenderSnapshot& outSnapshot) const {
    synchronizer_.Sync(scene, renderScene_);
    renderScene_.BuildSnapshotInto(viewportWidth, viewportHeight, outSnapshot);
}

EcsRenderSceneSynchronizerStats SceneRenderExtractor::SyncStats() const noexcept {
    return synchronizer_.Stats();
}

} // namespace kb::render
