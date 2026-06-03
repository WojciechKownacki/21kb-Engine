#include "kb/render/frame/FullscreenTexturePass.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

namespace kb::render {
namespace {

struct PosTexVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

[[nodiscard]] bgfx::VertexLayout FullscreenVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] float TonemapOperatorValue(FullscreenTextureTonemapOperator tonemap) noexcept {
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

[[nodiscard]] std::vector<std::uint8_t> MakeNeutralColorGradeLut(std::uint16_t size) {
    constexpr std::size_t kChannelCount = 4U;
    const std::size_t voxelCount = static_cast<std::size_t>(size) * size * size;
    std::vector<std::uint8_t> pixels(voxelCount * kChannelCount);
    for (std::uint16_t z = 0; z < size; ++z) {
        for (std::uint16_t y = 0; y < size; ++y) {
            for (std::uint16_t x = 0; x < size; ++x) {
                const std::size_t offset = ((static_cast<std::size_t>(z) * size + y) * size + x) * kChannelCount;
                pixels[offset + 0U] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(x) * UINT8_MAX) / (size - 1U));
                pixels[offset + 1U] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(y) * UINT8_MAX) / (size - 1U));
                pixels[offset + 2U] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(z) * UINT8_MAX) / (size - 1U));
                pixels[offset + 3U] = UINT8_MAX;
            }
        }
    }
    return pixels;
}

[[nodiscard]] bgfx::TextureHandle CreateNeutralColorGradeLut() {
    constexpr std::uint16_t kLutSize = 16U;
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_TEXTURE_3D) == 0U) {
        return BGFX_INVALID_HANDLE;
    }

    const std::vector<std::uint8_t> pixels = MakeNeutralColorGradeLut(kLutSize);
    constexpr std::uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    const bgfx::Memory* memory = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    bgfx::TextureHandle texture = bgfx::createTexture3D(kLutSize, kLutSize, kLutSize, false, bgfx::TextureFormat::RGBA8, flags, memory);
    if (bgfx::isValid(texture)) {
        bgfx::setName(texture, "KB Neutral Color Grade LUT");
    }
    return texture;
}

} // namespace

FullscreenTexturePass::~FullscreenTexturePass() {
    Shutdown();
}

float ResolveFullscreenTextureExposureStops(const FullscreenTextureOutputTransform& transform) noexcept {
    if (!transform.autoExposure.enabled) {
        return transform.exposureStops;
    }

    const float luminance = std::max(transform.autoExposure.meteredAverageLuminance, 0.0001F);
    const float middleGray = std::max(transform.autoExposure.middleGray, 0.0001F);
    const float minStops = std::min(transform.autoExposure.minExposureStops, transform.autoExposure.maxExposureStops);
    const float maxStops = std::max(transform.autoExposure.minExposureStops, transform.autoExposure.maxExposureStops);
    const float autoStops = std::log2(middleGray / luminance) + transform.autoExposure.biasStops;
    return std::clamp(autoStops, minStops, maxStops);
}

bool FullscreenTexturePassDesc::IsValid() const noexcept {
    return bgfx::isValid(sourceTexture) && extent.IsValid() && (!outputRect.extent.IsValid() || outputRect.IsValid());
}

bool FullscreenTexturePass::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_present_tex.sc");
    colorSampler_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    colorGradeSampler_ = bgfx::createUniform("s_colorGradeLut", bgfx::UniformType::Sampler);
    tonemapParams_ = bgfx::createUniform("u_tonemapParams", bgfx::UniformType::Vec4);
    colorGradeParams_ = bgfx::createUniform("u_colorGradeParams", bgfx::UniformType::Vec4);
    neutralColorGradeLut_ = CreateNeutralColorGradeLut();
    if (!bgfx::isValid(program_) || !bgfx::isValid(colorSampler_) || !bgfx::isValid(colorGradeSampler_) ||
        !bgfx::isValid(tonemapParams_) || !bgfx::isValid(colorGradeParams_) || !bgfx::isValid(neutralColorGradeLut_)) {
        Shutdown();
        return false;
    }

    return true;
}

void FullscreenTexturePass::Shutdown() noexcept {
    if (bgfx::isValid(neutralColorGradeLut_)) {
        bgfx::destroy(neutralColorGradeLut_);
        neutralColorGradeLut_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(colorGradeParams_)) {
        bgfx::destroy(colorGradeParams_);
        colorGradeParams_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(tonemapParams_)) {
        bgfx::destroy(tonemapParams_);
        tonemapParams_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(colorGradeSampler_)) {
        bgfx::destroy(colorGradeSampler_);
        colorGradeSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(colorSampler_)) {
        bgfx::destroy(colorSampler_);
        colorSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
}

bool FullscreenTexturePass::Submit(const FullscreenTexturePassDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    const bgfx::VertexLayout layout = FullscreenVertexLayout();
    constexpr std::array<PosTexVertex, 3> triangle{
        PosTexVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        PosTexVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        PosTexVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };

    constexpr std::uint32_t vertexCount = static_cast<std::uint32_t>(triangle.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout) < vertexCount) {
        return false;
    }

    const std::array<float, 16> identity = IdentityMatrix();
    const RenderViewportRect outputRect = desc.outputRect.extent.IsValid()
        ? desc.outputRect
        : RenderViewportRect{.extent = desc.extent};
    const std::uint16_t x = ClampToViewExtent(outputRect.x);
    const std::uint16_t y = ClampToViewExtent(outputRect.y);
    const std::uint16_t width = ClampToViewExtent(outputRect.extent.width);
    const std::uint16_t height = ClampToViewExtent(outputRect.extent.height);

    bgfx::setViewName(desc.viewId, desc.viewName == nullptr ? "KB Fullscreen Texture" : desc.viewName);
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewRect(desc.viewId, x, y, width, height);
    bgfx::setViewTransform(desc.viewId, identity.data(), identity.data());
    bgfx::setViewClear(desc.viewId, desc.clearTarget ? BGFX_CLEAR_COLOR : BGFX_CLEAR_NONE, desc.clearRgba);
    bgfx::touch(desc.viewId);

    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    std::memcpy(vertices.data, triangle.data(), sizeof(PosTexVertex) * triangle.size());

    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    const float gamma = std::max(desc.outputTransform.gamma, 0.001F);
    constexpr float kLutSize = 16.0F;
    const float tonemapParams[4] = {
        ResolveFullscreenTextureExposureStops(desc.outputTransform),
        1.0F / gamma,
        TonemapOperatorValue(desc.outputTransform.tonemap),
        0.0F,
    };
    const float colorGradeParams[4] = {
        kLutSize,
        std::clamp(desc.outputTransform.colorGradingLutStrength, 0.0F, 1.0F),
        0.0F,
        0.0F,
    };
    bgfx::setUniform(tonemapParams_, tonemapParams);
    bgfx::setUniform(colorGradeParams_, colorGradeParams);
    bgfx::setTexture(0, colorSampler_, desc.sourceTexture);
    bgfx::setTexture(1, colorGradeSampler_, neutralColorGradeLut_);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);
    return true;
}

bool FullscreenTexturePass::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(colorSampler_) && bgfx::isValid(colorGradeSampler_) &&
           bgfx::isValid(tonemapParams_) && bgfx::isValid(colorGradeParams_) && bgfx::isValid(neutralColorGradeLut_);
}

} // namespace kb::render
