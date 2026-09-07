#include "private/ui/ScreenUIBackgroundBlur.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace kb::render {
namespace {

struct FullscreenVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

[[nodiscard]] bgfx::VertexLayout FullscreenLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
        .end();
    return layout;
}

constexpr std::array<float, 16> kIdentity{
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
};

} // namespace

ScreenUIBackgroundBlur::~ScreenUIBackgroundBlur() {
    Shutdown();
}

bool ScreenUIBackgroundBlur::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_blur.sc");
    sourceSampler_ = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);
    paramsUniform_ = bgfx::createUniform("u_postParams", bgfx::UniformType::Vec4);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void ScreenUIBackgroundBlur::Shutdown() noexcept {
    InvalidateTargets();
    if (bgfx::isValid(paramsUniform_)) {
        bgfx::destroy(paramsUniform_);
        paramsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(sourceSampler_)) {
        bgfx::destroy(sourceSampler_);
        sourceSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
}

void ScreenUIBackgroundBlur::InvalidateTargets() noexcept {
    for (TargetSet& targets : targets_) {
        Release(targets);
    }
}

bool ScreenUIBackgroundBlur::Submit(std::uint32_t viewportIndex, RenderExtent extent,
                                    bgfx::TextureFormat::Enum format, bgfx::TextureHandle source,
                                    std::uint16_t horizontalView, std::uint16_t verticalView, float radius) {
    if (!IsInitialized() || !bgfx::isValid(source) || !Ensure(viewportIndex, extent, format)) {
        return false;
    }
    TargetSet& targets = targets_[viewportIndex];
    const float sampleRadius = std::clamp(radius, 1.0F, 64.0F);
    if (!SubmitPass(horizontalView, extent, targets.pingFrameBuffer, source,
                    sampleRadius / static_cast<float>(extent.width), 0.0F, "KB Screen UI Blur H")) {
        return false;
    }
    return SubmitPass(verticalView, extent, targets.outputFrameBuffer, targets.pingTexture, 0.0F,
                      sampleRadius / static_cast<float>(extent.height), "KB Screen UI Blur V");
}

bgfx::TextureHandle ScreenUIBackgroundBlur::Output(std::uint32_t viewportIndex) const noexcept {
    return viewportIndex < targets_.size()
        ? targets_[viewportIndex].outputTexture
        : bgfx::TextureHandle{ bgfx::kInvalidHandle };
}

bool ScreenUIBackgroundBlur::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(sourceSampler_) && bgfx::isValid(paramsUniform_);
}

bool ScreenUIBackgroundBlur::Ensure(std::uint32_t viewportIndex, RenderExtent extent,
                                    bgfx::TextureFormat::Enum format) {
    if (viewportIndex >= targets_.size() || !extent.IsValid() || extent.width > UINT16_MAX || extent.height > UINT16_MAX) {
        return false;
    }
    if (format == bgfx::TextureFormat::Count) {
        format = bgfx::TextureFormat::RGBA16F;
    }
    TargetSet& targets = targets_[viewportIndex];
    if (targets.extent.width == extent.width && targets.extent.height == extent.height && targets.format == format &&
        bgfx::isValid(targets.pingTexture) && bgfx::isValid(targets.outputTexture)) {
        return true;
    }
    Release(targets);
    constexpr std::uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    if (!bgfx::isTextureValid(0U, false, 1U, format, flags)) {
        return false;
    }
    targets.pingTexture = bgfx::createTexture2D(static_cast<std::uint16_t>(extent.width),
                                                static_cast<std::uint16_t>(extent.height), false, 1U, format, flags);
    targets.outputTexture = bgfx::createTexture2D(static_cast<std::uint16_t>(extent.width),
                                                  static_cast<std::uint16_t>(extent.height), false, 1U, format, flags);
    if (!bgfx::isValid(targets.pingTexture) || !bgfx::isValid(targets.outputTexture)) {
        Release(targets);
        return false;
    }
    bgfx::setName(targets.pingTexture, "KB Screen UI Blur Ping");
    bgfx::setName(targets.outputTexture, "KB Screen UI Blurred");
    targets.pingFrameBuffer = bgfx::createFrameBuffer(1U, &targets.pingTexture, false);
    targets.outputFrameBuffer = bgfx::createFrameBuffer(1U, &targets.outputTexture, false);
    if (!bgfx::isValid(targets.pingFrameBuffer) || !bgfx::isValid(targets.outputFrameBuffer)) {
        Release(targets);
        return false;
    }
    targets.extent = extent;
    targets.format = format;
    return true;
}

void ScreenUIBackgroundBlur::Release(TargetSet& targets) noexcept {
    if (bgfx::isValid(targets.pingFrameBuffer)) {
        bgfx::destroy(targets.pingFrameBuffer);
    }
    if (bgfx::isValid(targets.outputFrameBuffer)) {
        bgfx::destroy(targets.outputFrameBuffer);
    }
    if (bgfx::isValid(targets.pingTexture)) {
        bgfx::destroy(targets.pingTexture);
    }
    if (bgfx::isValid(targets.outputTexture)) {
        bgfx::destroy(targets.outputTexture);
    }
    targets = TargetSet{};
}

bool ScreenUIBackgroundBlur::SubmitPass(std::uint16_t viewId, RenderExtent extent, bgfx::FrameBufferHandle output,
                                        bgfx::TextureHandle source, float stepX, float stepY,
                                        const char* viewName) const {
    const bgfx::VertexLayout layout = FullscreenLayout();
    constexpr std::array<FullscreenVertex, 3U> triangle{
        FullscreenVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        FullscreenVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        FullscreenVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(triangle.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout) < vertexCount) {
        return false;
    }
    bgfx::setViewName(viewId, viewName);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(viewId, output);
    bgfx::setViewRect(viewId, 0U, 0U, static_cast<std::uint16_t>(extent.width), static_cast<std::uint16_t>(extent.height));
    bgfx::setViewTransform(viewId, kIdentity.data(), kIdentity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::touch(viewId);

    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    std::memcpy(vertices.data, triangle.data(), sizeof(triangle));
    const float params[4]{stepX, stepY, 0.0F, 0.0F};
    bgfx::setUniform(paramsUniform_, params);
    bgfx::setTexture(0U, sourceSampler_, source);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0U, &vertices);
    bgfx::submit(viewId, program_);
    return true;
}

} // namespace kb::render
