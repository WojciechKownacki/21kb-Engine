#pragma once

#include "kb/render/frame/EditorRenderPassSubmitter.hpp"
#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

namespace kb::render {

class RendererEditorOverlaySubmitter final {
public:
    static void Submit(
        const EditorRenderPassSubmitter& editorPassSubmitter,
        const RenderViewportPlan& viewportPlan,
        const RenderSceneSubmitDesc& desc,
        const SceneRenderCamera* overlayCamera,
        bool selectionOutlineEnabled);
};

} // namespace kb::render
