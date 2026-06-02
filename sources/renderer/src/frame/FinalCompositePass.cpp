#include "kb/render/frame/FinalCompositePass.hpp"

namespace kb::render {

FinalCompositePass::~FinalCompositePass() {
    Shutdown();
}

bool FinalCompositePassDesc::IsValid() const noexcept {
    return bgfx::isValid(postProcessColor) && extent.IsValid();
}

bool FinalCompositePass::Initialize() {
    return displayComposite_.Initialize();
}

void FinalCompositePass::Shutdown() noexcept {
    displayComposite_.Shutdown();
}

bool FinalCompositePass::Submit(const FinalCompositePassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    return displayComposite_.Submit(SceneDisplayCompositeDesc{
        .viewId = desc.viewId,
        .hdrColor = desc.postProcessColor,
        .frameBuffer = desc.frameBuffer,
        .extent = desc.extent,
        .outputTransform = desc.outputTransform,
        .clearRgba = desc.clearRgba,
        .clearTarget = desc.clearTarget,
    });
}

bool FinalCompositePass::IsInitialized() const noexcept {
    return displayComposite_.IsInitialized();
}

} // namespace kb::render
