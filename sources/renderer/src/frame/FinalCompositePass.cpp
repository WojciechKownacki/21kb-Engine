#include "kb/render/frame/FinalCompositePass.hpp"

#include "renderer/RendererDebugLog.hpp"

#include <sstream>

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
}

} // namespace

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
        std::ostringstream message;
        message << "Submit invalid initialized=" << (IsInitialized() ? "true" : "false")
                << " descValid=" << (desc.IsValid() ? "true" : "false")
                << " colorTex=" << HandleValue(desc.postProcessColor)
                << " fb=" << HandleValue(desc.frameBuffer)
                << " extent=" << desc.extent.width << 'x' << desc.extent.height;
        WriteRendererDebugLog("final_composite", message.str());
        return false;
    }

    {
        std::ostringstream message;
        message << "Submit begin viewId=" << desc.viewId
                << " colorTex=" << HandleValue(desc.postProcessColor)
                << " fb=" << HandleValue(desc.frameBuffer)
                << " extent=" << desc.extent.width << 'x' << desc.extent.height
                << " outputRect=" << desc.outputRect.x << ',' << desc.outputRect.y << ' '
                << desc.outputRect.extent.width << 'x' << desc.outputRect.extent.height
                << " clear=" << (desc.clearTarget ? "true" : "false")
                << " clearRgba=0x" << std::hex << desc.clearRgba << std::dec
                << " tonemap=" << static_cast<int>(desc.outputTransform.tonemap)
                << " exposure=" << desc.outputTransform.exposureStops
                << " gamma=" << desc.outputTransform.gamma;
        WriteRendererDebugLog("final_composite", message.str());
    }
    const bool submitted = displayComposite_.Submit(SceneDisplayCompositeDesc{
        .viewId = desc.viewId,
        .hdrColor = desc.postProcessColor,
        .frameBuffer = desc.frameBuffer,
        .extent = desc.extent,
        .outputRect = desc.outputRect,
        .outputTransform = desc.outputTransform,
        .clearRgba = desc.clearRgba,
        .clearTarget = desc.clearTarget,
    });
    WriteRendererDebugLog("final_composite", submitted ? "Submit end ok" : "Submit end failed");
    return submitted;
}

bool FinalCompositePass::IsInitialized() const noexcept {
    return displayComposite_.IsInitialized();
}

} // namespace kb::render
