#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"

namespace kb::render {

SceneDisplayCompositeRenderer::~SceneDisplayCompositeRenderer() {
    Shutdown();
}

bool SceneDisplayCompositeDesc::IsValid() const noexcept {
    return bgfx::isValid(hdrColor) && extent.IsValid();
}

bool SceneDisplayCompositeRenderer::Initialize() {
    return fullscreenPass_.Initialize();
}

void SceneDisplayCompositeRenderer::Shutdown() noexcept {
    fullscreenPass_.Shutdown();
}

bool SceneDisplayCompositeRenderer::Submit(const SceneDisplayCompositeDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    return fullscreenPass_.Submit(FullscreenTexturePassDesc{
        .viewId = desc.viewId,
        .sourceTexture = desc.hdrColor,
        .frameBuffer = desc.frameBuffer,
        .extent = desc.extent,
        .outputTransform = desc.outputTransform,
        .clearRgba = desc.clearRgba,
        .clearTarget = desc.clearTarget,
        .viewName = "KB Display Composite",
    });
}

bool SceneDisplayCompositeRenderer::IsInitialized() const noexcept {
    return fullscreenPass_.IsInitialized();
}

} // namespace kb::render
