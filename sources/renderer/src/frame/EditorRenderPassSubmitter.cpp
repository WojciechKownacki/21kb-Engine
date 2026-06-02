#include "kb/render/frame/EditorRenderPassSubmitter.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

void SubmitEmptyEditorView(
    bgfx::ViewId viewId,
    bgfx::FrameBufferHandle frameBuffer,
    RenderExtent extent,
    const char* viewName) {
    if (!extent.IsValid()) {
        return;
    }

    const std::array<float, 16> identity = IdentityMatrix();
    bgfx::setViewName(viewId, viewName);
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewRect(viewId, 0, 0, ClampToViewExtent(extent.width), ClampToViewExtent(extent.height));
    bgfx::touch(viewId);
}

} // namespace

void EditorRenderPassSubmitter::SubmitSelectionMask(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const {
    SubmitEmptyEditorView(
        viewportPlan.viewIds.selectionMask,
        desc.target.frameBuffer,
        desc.target.viewport.extent,
        "KB Editor Selection Mask");
}

void EditorRenderPassSubmitter::SubmitSceneOverlays(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const {
    SubmitEmptyEditorView(
        viewportPlan.viewIds.sceneOverlays,
        desc.target.frameBuffer,
        desc.target.viewport.extent,
        "KB Editor Scene Overlays");
}

void EditorRenderPassSubmitter::SubmitUiComposite(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const {
    const RenderExtent extent = desc.finalComposite.enabled ? desc.finalComposite.extent : desc.target.viewport.extent;
    const bgfx::FrameBufferHandle frameBuffer = desc.finalComposite.enabled ? desc.finalComposite.frameBuffer : desc.target.frameBuffer;
    SubmitEmptyEditorView(
        viewportPlan.viewIds.editorUiComposite,
        frameBuffer,
        extent,
        "KB Editor UI Composite");
}

} // namespace kb::render
