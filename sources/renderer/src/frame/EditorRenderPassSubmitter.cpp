#include "kb/render/frame/EditorRenderPassSubmitter.hpp"

#include "renderer/RendererDebugLog.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <sstream>

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "true" : "false";
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] bool SameHandle(bgfx::TextureHandle lhs, bgfx::TextureHandle rhs) noexcept {
    return lhs.idx == rhs.idx;
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

bool EditorRenderPassSubmitter::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    if (!gridPass_.Initialize() || !gizmoPass_.Initialize() || !selectionBoxPass_.Initialize() || !selectionOutlinePass_.Initialize()) {
        Shutdown();
        return false;
    }
    return true;
}

void EditorRenderPassSubmitter::Shutdown() noexcept {
    selectionOutlinePass_.Shutdown();
    selectionBoxPass_.Shutdown();
    gizmoPass_.Shutdown();
    gridPass_.Shutdown();
}

void EditorRenderPassSubmitter::InvalidateFrameBuffers() noexcept {
    gridPass_.InvalidateFrameBuffers();
}

bool EditorRenderPassSubmitter::IsInitialized() const noexcept {
    return gridPass_.IsInitialized() && gizmoPass_.IsInitialized() && selectionBoxPass_.IsInitialized() && selectionOutlinePass_.IsInitialized();
}

void EditorRenderPassSubmitter::SubmitSelectionMask(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc) const {
    if (desc.postProcess.enabled && bgfx::isValid(desc.postProcess.selectionMaskFrameBuffer)) {
        if (!desc.postProcess.extent.IsValid()) {
            return;
        }

        const std::array<float, 16> identity = IdentityMatrix();
        bgfx::setViewName(viewportPlan.viewIds.selectionMask, "KB Editor Selection Mask");
        bgfx::setViewFrameBuffer(viewportPlan.viewIds.selectionMask, desc.postProcess.selectionMaskFrameBuffer);
        bgfx::setViewTransform(viewportPlan.viewIds.selectionMask, identity.data(), identity.data());
        bgfx::setViewClear(viewportPlan.viewIds.selectionMask, BGFX_CLEAR_COLOR, 0x00000000U, desc.clearDepth, 0U);
        bgfx::setViewRect(viewportPlan.viewIds.selectionMask, 0, 0, ClampToViewExtent(desc.postProcess.extent.width), ClampToViewExtent(desc.postProcess.extent.height));
        bgfx::touch(viewportPlan.viewIds.selectionMask);
        return;
    }

    SubmitEmptyEditorView(
        viewportPlan.viewIds.selectionMask,
        desc.target.frameBuffer,
        desc.target.viewport.extent,
        "KB Editor Selection Mask");
}

void EditorRenderPassSubmitter::SubmitSceneOverlays(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc, const SceneRenderCamera* camera) const {
    if (camera != nullptr && IsInitialized()) {
        const bgfx::TextureHandle depthTexture = desc.SceneOverlayDepthTexture();
        const bool buildDepthFrameBuffer = bgfx::isValid(depthTexture) && !SameHandle(depthTexture, desc.target.depthTexture);
        {
            std::ostringstream message;
            message << "Scene grid route"
                    << " viewId=" << viewportPlan.viewIds.sceneOverlays
                    << " targetFb=" << HandleValue(desc.target.frameBuffer)
                    << " targetColor=" << HandleValue(desc.target.colorTexture)
                    << " targetDepth=" << HandleValue(desc.target.depthTexture)
                    << " overlayDepth=" << HandleValue(depthTexture)
                    << " buildDepthFb=" << BoolText(buildDepthFrameBuffer)
                    << " finalComposite=" << BoolText(desc.finalComposite.enabled)
                    << " postProcess=" << BoolText(desc.postProcessEnabled)
                    << " msaaSamples=" << static_cast<unsigned>(desc.target.msaaSamples)
                    << " extent=" << desc.target.viewport.extent.width << 'x' << desc.target.viewport.extent.height;
            WriteRendererDebugLog("grid_trace", message.str());
        }
        if (desc.editorGrid.visible) {
            const SceneGridPassDesc gridDesc{
                .viewId = viewportPlan.viewIds.sceneOverlays,
                .frameBuffer = desc.target.frameBuffer,
                .colorTexture = desc.target.colorTexture,
                .depthTexture = depthTexture,
                .extent = desc.target.viewport.extent,
                .camera = camera,
                .minorSpacingMeters = desc.editorGrid.minorSpacingMeters,
                .majorEvery = desc.editorGrid.majorEvery,
                .buildDepthFrameBuffer = buildDepthFrameBuffer,
            };
            static_cast<void>(gridPass_.Submit(gridDesc));
        } else {
            SubmitEmptyEditorView(
                viewportPlan.viewIds.sceneOverlays,
                desc.target.frameBuffer,
                desc.target.viewport.extent,
                "KB Editor Scene Overlays");
        }
        return;
    }

    SubmitEmptyEditorView(
        viewportPlan.viewIds.sceneOverlays,
        desc.target.frameBuffer,
        desc.target.viewport.extent,
        "KB Editor Scene Overlays");
}

void EditorRenderPassSubmitter::SubmitUiComposite(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc, bool selectionOutlineEnabled) const {
    const RenderExtent extent = desc.finalComposite.enabled ? desc.finalComposite.extent : desc.target.viewport.extent;
    const bgfx::FrameBufferHandle frameBuffer = desc.finalComposite.enabled ? desc.finalComposite.frameBuffer : desc.target.frameBuffer;
    const RenderViewportRect outputRect = desc.finalComposite.enabled ? desc.finalComposite.outputRect : RenderViewportRect{};
    if (IsInitialized()) {
        static_cast<void>(selectionBoxPass_.Submit(SelectionBoxOverlayPassDesc{
            .viewId = viewportPlan.viewIds.editorUiComposite,
            .frameBuffer = frameBuffer,
            .extent = extent,
            .outputRect = outputRect,
            .x = desc.editorSelectionBox.x,
            .y = desc.editorSelectionBox.y,
            .width = desc.editorSelectionBox.width,
            .height = desc.editorSelectionBox.height,
            .visible = desc.editorSelectionBox.visible,
        }));
    }
    if (selectionOutlineEnabled && IsInitialized() && desc.finalComposite.enabled && bgfx::isValid(desc.postProcess.selectionMaskTexture) && !desc.selectedEntityIds.empty()) {
        static_cast<void>(selectionOutlinePass_.Submit(SelectionOutlineCompositePassDesc{
            .viewId = viewportPlan.viewIds.editorUiComposite,
            .selectionMask = desc.postProcess.selectionMaskTexture,
            .frameBuffer = frameBuffer,
            .extent = extent,
            .outputRect = outputRect,
        }));
    }
    SubmitEmptyEditorView(
        viewportPlan.viewIds.editorUiComposite,
        frameBuffer,
        extent,
        "KB Editor UI Composite");
}

void EditorRenderPassSubmitter::SubmitGizmoOverlay(const RenderViewportPlan& viewportPlan, const RenderSceneSubmitDesc& desc, const SceneRenderCamera* camera) const {
    if (camera != nullptr && IsInitialized()) {
        const bool overlayFinalTarget = desc.finalComposite.enabled;
        const bgfx::FrameBufferHandle frameBuffer = overlayFinalTarget ? desc.finalComposite.frameBuffer : desc.target.frameBuffer;
        const RenderExtent extent = overlayFinalTarget ? desc.finalComposite.extent : desc.target.viewport.extent;
        const RenderViewportRect outputRect = overlayFinalTarget ? desc.finalComposite.outputRect : RenderViewportRect{};
        static_cast<void>(gizmoPass_.Submit(SceneGizmoPassDesc{
            .viewId = viewportPlan.viewIds.editorGizmoOverlay,
            .frameBuffer = frameBuffer,
            .extent = extent,
            .outputRect = outputRect,
            .camera = camera,
            .targetPosition = desc.editorGizmo.targetPosition,
            .worldScale = desc.editorGizmo.worldScale,
            .hoveredAxis = desc.editorGizmo.hoveredAxis,
            .draggedAxis = desc.editorGizmo.draggedAxis,
            .mode = desc.editorGizmo.mode,
            .lightWireframes = desc.editorLightWireframes,
            .visible = desc.editorGizmo.visible,
        }));
        return;
    }

    SubmitEmptyEditorView(
        viewportPlan.viewIds.editorGizmoOverlay,
        desc.finalComposite.enabled ? desc.finalComposite.frameBuffer : desc.target.frameBuffer,
        desc.finalComposite.enabled ? desc.finalComposite.extent : desc.target.viewport.extent,
        "KB Editor Gizmo Overlay");
}

} // namespace kb::render
