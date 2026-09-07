#include "private/ui/UserWidgetComposite.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace kb::render {
namespace {

[[nodiscard]] bgfx::VertexLayout WidgetVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 2U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2U, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 2U, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::uint16_t ToU16(float value) noexcept {
    return static_cast<std::uint16_t>(std::clamp(value, 0.0F, static_cast<float>(UINT16_MAX)));
}

[[nodiscard]] float TonemapValue(FullscreenTextureTonemapOperator tonemap) noexcept {
    switch (tonemap) {
    case FullscreenTextureTonemapOperator::None:
        return -1.0F;
    case FullscreenTextureTonemapOperator::Aces:
        return 0.0F;
    case FullscreenTextureTonemapOperator::AgxApprox:
        return 1.0F;
    }
    return 0.0F;
}

} // namespace

bool UserWidgetCompositeDesc::IsValid() const noexcept {
    return presentationExtent.IsValid() && outputExtent.IsValid() && drawList != nullptr &&
           (!outputRect.extent.IsValid() || outputRect.IsValid());
}

UserWidgetComposite::~UserWidgetComposite() {
    Shutdown();
}

bool UserWidgetComposite::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_user_widget.sc", "fs_user_widget.sc");
    sampler_ = bgfx::createUniform("s_widgetTexture", bgfx::UniformType::Sampler);
    viewportUniform_ = bgfx::createUniform("u_widgetViewport", bgfx::UniformType::Vec4);
    fillColorUniform_ = bgfx::createUniform("u_widgetFillColor", bgfx::UniformType::Vec4);
    borderColorUniform_ = bgfx::createUniform("u_widgetBorderColor", bgfx::UniformType::Vec4);
    borderWidthsUniform_ = bgfx::createUniform("u_widgetBorderWidths", bgfx::UniformType::Vec4);
    cornerRadiiUniform_ = bgfx::createUniform("u_widgetCornerRadii", bgfx::UniformType::Vec4);
    styleParamsUniform_ = bgfx::createUniform("u_widgetStyleParams", bgfx::UniformType::Vec4);
    effectParamsUniform_ = bgfx::createUniform("u_widgetEffectParams", bgfx::UniformType::Vec4);
    tonemapParamsUniform_ = bgfx::createUniform("u_widgetTonemapParams", bgfx::UniformType::Vec4);
    constexpr std::array<std::uint8_t, 4U> white{255U, 255U, 255U, 255U};
    whiteTexture_ = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
                                          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                          bgfx::copy(white.data(), static_cast<std::uint32_t>(white.size())));
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void UserWidgetComposite::Shutdown() noexcept {
    if (bgfx::isValid(whiteTexture_)) {
        bgfx::destroy(whiteTexture_);
        whiteTexture_ = BGFX_INVALID_HANDLE;
    }
    const std::array<bgfx::UniformHandle*, 8U> uniforms{
        &sampler_,
        &viewportUniform_,
        &fillColorUniform_,
        &borderColorUniform_,
        &borderWidthsUniform_,
        &cornerRadiiUniform_,
        &styleParamsUniform_,
        &effectParamsUniform_,
    };
    for (bgfx::UniformHandle* uniform : uniforms) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(tonemapParamsUniform_)) {
        bgfx::destroy(tonemapParamsUniform_);
        tonemapParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
}

bool UserWidgetComposite::Submit(const UserWidgetCompositeDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }
    const UserWidgetDrawList& drawList = *desc.drawList;
    const RenderViewportRect outputRect =
        desc.outputRect.extent.IsValid() ? desc.outputRect : RenderViewportRect{.extent = desc.outputExtent};
    bgfx::setViewName(desc.viewId, "KB Runtime UI Composite");
    bgfx::setViewMode(desc.viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewRect(desc.viewId, ToU16(static_cast<float>(outputRect.x)), ToU16(static_cast<float>(outputRect.y)),
                      ToU16(static_cast<float>(outputRect.extent.width)),
                      ToU16(static_cast<float>(outputRect.extent.height)));
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);
    bgfx::touch(desc.viewId);
    if (drawList.indices.empty() || drawList.vertices.empty()) {
        return true;
    }

    const bgfx::VertexLayout layout = WidgetVertexLayout();
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(drawList.vertices.size());
    const std::uint32_t indexCount = static_cast<std::uint32_t>(drawList.indices.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout) < vertexCount ||
        bgfx::getAvailTransientIndexBuffer(indexCount, true) < indexCount) {
        return false;
    }
    bgfx::TransientVertexBuffer vertices{};
    bgfx::TransientIndexBuffer indices{};
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    bgfx::allocTransientIndexBuffer(&indices, indexCount, true);
    std::memcpy(vertices.data, drawList.vertices.data(), sizeof(UserWidgetVertex) * drawList.vertices.size());
    std::memcpy(indices.data, drawList.indices.data(), sizeof(std::uint32_t) * drawList.indices.size());

    const float viewport[4]{
        static_cast<float>(desc.presentationExtent.width),
        static_cast<float>(desc.presentationExtent.height),
        1.0F / static_cast<float>(desc.presentationExtent.width),
        1.0F / static_cast<float>(desc.presentationExtent.height),
    };
    const float gamma = std::max(desc.outputTransform.gamma, 0.001F);
    const float tonemap[4]{
        ResolveFullscreenTextureExposureStops(desc.outputTransform),
        1.0F / gamma,
        TonemapValue(desc.outputTransform.tonemap),
        0.0F,
    };
    bgfx::setUniform(viewportUniform_, viewport);
    bgfx::setUniform(tonemapParamsUniform_, tonemap);

    const float outputScaleX =
        static_cast<float>(outputRect.extent.width) / static_cast<float>(desc.presentationExtent.width);
    const float outputScaleY =
        static_cast<float>(outputRect.extent.height) / static_cast<float>(desc.presentationExtent.height);
    for (const UserWidgetDrawBatch& batch : drawList.batches) {
        bgfx::TextureHandle texture = whiteTexture_;
        std::uint16_t textureWidth = 1U;
        std::uint16_t textureHeight = 1U;
        if (batch.texture.source != UserWidgetTextureSource::None) {
            const UserWidgetResolvedTexture* resolved = Resolve(desc.textures, batch.texture);
            if (resolved == nullptr || !bgfx::isValid(resolved->texture)) {
                continue;
            }
            texture = resolved->texture;
            textureWidth = std::max(resolved->width, static_cast<std::uint16_t>(1U));
            textureHeight = std::max(resolved->height, static_cast<std::uint16_t>(1U));
        }
        const float styleParams[4]{
            batch.style.width,
            batch.style.height,
            batch.style.opacity,
            batch.style.feather,
        };
        const float effectParams[4]{
            static_cast<float>(batch.style.fragmentKind),
            batch.style.fontOutlineWidth,
            static_cast<float>(textureWidth),
            static_cast<float>(textureHeight),
        };
        bgfx::setUniform(fillColorUniform_, batch.style.fillColor.data());
        bgfx::setUniform(borderColorUniform_, batch.style.borderColor.data());
        bgfx::setUniform(borderWidthsUniform_, batch.style.borderWidths.data());
        bgfx::setUniform(cornerRadiiUniform_, batch.style.cornerRadii.data());
        bgfx::setUniform(styleParamsUniform_, styleParams);
        bgfx::setUniform(effectParamsUniform_, effectParams);
        bgfx::setTexture(0U, sampler_, texture);
        const float outputLeft = static_cast<float>(outputRect.x);
        const float outputTop = static_cast<float>(outputRect.y);
        const float outputRight = outputLeft + static_cast<float>(outputRect.extent.width);
        const float outputBottom = outputTop + static_cast<float>(outputRect.extent.height);
        const float clipLeft = std::clamp(outputLeft + batch.clipRect.left * outputScaleX, outputLeft, outputRight);
        const float clipTop = std::clamp(outputTop + batch.clipRect.top * outputScaleY, outputTop, outputBottom);
        const float clipRight = std::clamp(outputLeft + batch.clipRect.right * outputScaleX, clipLeft, outputRight);
        const float clipBottom = std::clamp(outputTop + batch.clipRect.bottom * outputScaleY, clipTop, outputBottom);
        bgfx::setScissor(ToU16(clipLeft), ToU16(clipTop), ToU16(std::max(clipRight - clipLeft, 0.0F)),
                         ToU16(std::max(clipBottom - clipTop, 0.0F)));
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
        bgfx::setVertexBuffer(0U, &vertices, 0U, vertexCount);
        bgfx::setIndexBuffer(&indices, batch.firstIndex, batch.indexCount);
        bgfx::submit(desc.viewId, program_);
    }
    return true;
}

bool UserWidgetComposite::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(whiteTexture_) && bgfx::isValid(sampler_) &&
           bgfx::isValid(viewportUniform_) && bgfx::isValid(fillColorUniform_) && bgfx::isValid(borderColorUniform_) &&
           bgfx::isValid(borderWidthsUniform_) && bgfx::isValid(cornerRadiiUniform_) &&
           bgfx::isValid(styleParamsUniform_) && bgfx::isValid(effectParamsUniform_) &&
           bgfx::isValid(tonemapParamsUniform_);
}

const UserWidgetResolvedTexture* UserWidgetComposite::Resolve(std::span<const UserWidgetResolvedTexture> textures,
                                                              UserWidgetTextureKey key) const noexcept {
    const auto found = std::ranges::find(textures, key, &UserWidgetResolvedTexture::key);
    return found == textures.end() ? nullptr : &*found;
}

} // namespace kb::render
