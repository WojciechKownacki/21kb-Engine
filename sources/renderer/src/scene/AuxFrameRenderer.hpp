#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "engine/math/DVec3.hpp"

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

    // Planar mirrors (the Planar Reflection component): the scene mirrored in the most visible plane of the view
    // `source` is drawn into an HDR picture, which the lighting of that view then reads (PlanarReflectionFor).
    // At most this many mirror passes are drawn per frame; each one is a full extra pass of the scene.
    static constexpr std::uint32_t kMaxPlanarReflectionPasses = 2U;
    // Adds the mirror pass of one view to `submissions` (taking one of `availableViewportIndices`) and returns whether
    // it did. A view without a visible mirror, or with an orthographic camera, draws no pass and frees its picture.
    // The pass draws before the lit scene of the primary viewport (and of any viewport with a higher index), so the
    // picture is this frame's there; a view in a viewport below the pass's own sees the previous frame's.
    [[nodiscard]] bool CollectPlanarReflection(
        const kb::scene::Scene& scene,
        const RenderScene& renderScene,
        const RenderSceneSubmitDesc& source,
        std::span<const std::uint32_t> availableViewportIndices,
        std::uint64_t frameIndex,
        SceneRenderer& sceneRenderer,
        std::vector<AuxFramePreparedSubmission>& submissions);
    // The mirror of the view for its lighting, in the render space of `renderOrigin`; invalid when the view has none
    // this frame.
    [[nodiscard]] SceneRenderPlanarReflection PlanarReflectionFor(
        std::uint64_t sceneId, std::uint32_t viewportId, const kb::math::DVec3& renderOrigin, const RenderResourceRegistry& resources) const;
    void ReleaseScene(std::uint64_t sceneId, SceneRenderer* sceneRenderer) noexcept;
    void Shutdown(SceneRenderer* sceneRenderer) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::render
