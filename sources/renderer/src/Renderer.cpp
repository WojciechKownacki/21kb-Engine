#include "kb/render/Renderer.hpp"

#include "kb/render/BgfxContext.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/ViewIdPolicy.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(RenderSurface& surface, const DisplayConfig* config) {
    displayConfig_ = config == nullptr ? DisplayConfig{} : *config;
    context_ = std::make_unique<BgfxContext>();

    bgfx::RendererType::Enum preferred = bgfx::RendererType::Count;
    if (displayConfig_.preferredBgfxRendererType >= 0 && displayConfig_.preferredBgfxRendererType < static_cast<std::int32_t>(bgfx::RendererType::Count)) {
        preferred = static_cast<bgfx::RendererType::Enum>(displayConfig_.preferredBgfxRendererType);
    }

    if (!context_->Initialize(surface, displayConfig_, preferred)) {
        context_.reset();
        return false;
    }

    return true;
}

void Renderer::Shutdown() {
    frameActive_ = false;
    if (context_ != nullptr) {
        context_->Shutdown();
        context_.reset();
    }
}

bool Renderer::BeginFrame() {
    if (context_ == nullptr) {
        return false;
    }

    frameActive_ = context_->BeginFrame();
    return frameActive_;
}

void Renderer::EndFrame() {
    if (context_ == nullptr || !frameActive_) {
        return;
    }

    static_cast<void>(context_->EndFrame());
    frameActive_ = false;
}

void Renderer::SubmitClear(std::uint32_t rgba, float depth, std::uint8_t stencil) {
    if (context_ == nullptr || !context_->IsInitialized() || !frameActive_) {
        return;
    }

    float identity[16] = {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    bgfx::setViewName(ViewId::Scene3D, "KB Scene3D");
    bgfx::setViewTransform(ViewId::Scene3D, identity, identity);
    bgfx::setViewClear(ViewId::Scene3D, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, rgba, depth, stencil);
    bgfx::setViewRect(ViewId::Scene3D, 0, 0, static_cast<std::uint16_t>(context_->Width()), static_cast<std::uint16_t>(context_->Height()));
    bgfx::touch(ViewId::Scene3D);
}

void Renderer::OnResize(std::uint32_t width, std::uint32_t height) {
    if (context_ == nullptr || !context_->IsInitialized() || width == 0 || height == 0) {
        return;
    }

    context_->Reset(width, height, displayConfig_.ComputeResetFlags());
}

bool Renderer::IsInitialized() const noexcept {
    return context_ != nullptr && context_->IsInitialized();
}

bool Renderer::IsFrameActive() const noexcept {
    return frameActive_;
}

std::uint32_t Renderer::BackbufferWidth() const noexcept {
    return context_ == nullptr ? 0 : context_->Width();
}

std::uint32_t Renderer::BackbufferHeight() const noexcept {
    return context_ == nullptr ? 0 : context_->Height();
}

void* Renderer::NativeWindowHandle() const noexcept {
    return context_ == nullptr ? nullptr : context_->NativeWindowHandle();
}

} // namespace kb::render
