#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/overlay/SceneGridPass.hpp"
#include "kb/render/overlay/SelectionOutlineCompositePass.hpp"

namespace kb::render {

class EditorRenderPassSubmitter {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    void SubmitSelectionMask(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const;
    void SubmitSceneOverlays(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc, const SceneRenderCamera* camera) const;
    void SubmitUiComposite(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc, bool selectionOutlineEnabled) const;

private:
    SceneGridPass gridPass_;
    SelectionOutlineCompositePass selectionOutlinePass_;
};

} // namespace kb::render
