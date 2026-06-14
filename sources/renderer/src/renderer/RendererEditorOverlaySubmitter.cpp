#include "renderer/RendererEditorOverlaySubmitter.hpp"

namespace kb::render {

void RendererEditorOverlaySubmitter::Submit(
    const EditorRenderPassSubmitter& editorPassSubmitter,
    const RenderViewportPlan& viewportPlan,
    const RenderSceneSubmitDesc& desc,
    const SceneRenderCamera* overlayCamera,
    bool selectionOutlineEnabled) {
    editorPassSubmitter.SubmitSceneOverlays(viewportPlan, desc, desc.editorSceneOverlaysEnabled ? overlayCamera : nullptr);
    editorPassSubmitter.SubmitUiComposite(viewportPlan, desc, selectionOutlineEnabled);
    editorPassSubmitter.SubmitGizmoOverlay(viewportPlan, desc, desc.editorSceneOverlaysEnabled ? overlayCamera : nullptr);
}

} // namespace kb::render
