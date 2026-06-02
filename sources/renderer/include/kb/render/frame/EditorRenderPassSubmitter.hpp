#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"

namespace kb::render {

class EditorRenderPassSubmitter {
public:
    void SubmitSelectionMask(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const;
    void SubmitSceneOverlays(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const;
    void SubmitUiComposite(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const;
};

} // namespace kb::render
