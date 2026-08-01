#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::render {

class RenderResourceRegistry;
class RenderScene;
class SceneRenderer;

struct AuxFramePreparedSubmission {
    const kb::scene::Scene* scene = nullptr;
    RenderSceneSubmitDesc desc{};
};

struct AuxFramePanoramaConversion {
    bgfx::ViewId viewId = 0U;
};

// Renderer-owned state for kb21.view.aux-frame.  It reads the ECS component
// columns but stores only transient GPU targets and scheduling state.
class AuxFrameRenderer final {
public:
    AuxFrameRenderer();
    ~AuxFrameRenderer();

    AuxFrameRenderer(const AuxFrameRenderer&) = delete;
    AuxFrameRenderer& operator=(const AuxFrameRenderer&) = delete;

    void BeginFrame();
    [[nodiscard]] bool HasSceneOutputs(std::uint64_t sceneId) const noexcept;
    void Collect(
        const kb::scene::Scene& scene,
        const RenderScene& renderScene,
        const RenderSceneSubmitDesc& sourceDesc,
        std::span<const std::uint32_t> availableViewportIndices,
        std::uint64_t frameIndex,
        SceneRenderer& sceneRenderer,
        std::vector<AuxFramePreparedSubmission>& submissions,
        std::vector<AuxFramePanoramaConversion>& panoramaConversions);
    [[nodiscard]] bool SubmitPanoramaConversion(AuxFramePanoramaConversion conversion) const;
    void ReleaseScene(std::uint64_t sceneId, SceneRenderer* sceneRenderer) noexcept;
    void Shutdown(SceneRenderer* sceneRenderer) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::render
