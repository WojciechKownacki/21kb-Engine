#include "kb/render/post/ScenePostProcessRenderer.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <array>
#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

struct PosTexVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

[[nodiscard]] bgfx::VertexLayout FullscreenLayout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

void ConfigureFullscreenView(
    bgfx::ViewId viewId,
    bgfx::FrameBufferHandle frameBuffer,
    RenderExtent extent,
    const char* name) {
    bgfx::setViewName(viewId, name);
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewRect(viewId, 0, 0, ClampToViewExtent(extent.width), ClampToViewExtent(extent.height));
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(viewId);
}

void SubmitFullscreen(bgfx::ViewId viewId, bgfx::ProgramHandle program, bgfx::VertexBufferHandle vertexBuffer) {
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, vertexBuffer);
    bgfx::submit(viewId, program);
}

} // namespace

ScenePostProcessRenderer::~ScenePostProcessRenderer() {
    Shutdown();
}

bool ScenePostProcessSubmitDesc::IsValid() const noexcept {
    return bgfx::isValid(sceneColor) && target.IsValid() && viewIds.IsValid();
}

bool ScenePostProcessRenderer::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    prefilterProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_prefilter.sc");
    blurProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_blur.sc");
    combineProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_combine.sc");
    sourceSampler_ = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);
    bloomSampler_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    postParams_ = bgfx::createUniform("u_postParams", bgfx::UniformType::Vec4);
    fullscreenLayout_ = FullscreenLayout();

    constexpr std::array<PosTexVertex, 3U> triangle{
        PosTexVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        PosTexVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        PosTexVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };
    const bgfx::Memory* memory = bgfx::copy(triangle.data(), static_cast<std::uint32_t>(sizeof(triangle)));
    fullscreenVertexBuffer_ = bgfx::createVertexBuffer(memory, fullscreenLayout_);
    if (bgfx::isValid(fullscreenVertexBuffer_)) {
        bgfx::setName(fullscreenVertexBuffer_, "KB Scene PostProcess Fullscreen Triangle");
    }

    if (!IsInitialized()) {
        Shutdown();
        return false;
    }

    return true;
}

void ScenePostProcessRenderer::Shutdown() noexcept {
    if (bgfx::isValid(fullscreenVertexBuffer_)) {
        bgfx::destroy(fullscreenVertexBuffer_);
        fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(postParams_)) {
        bgfx::destroy(postParams_);
        postParams_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(bloomSampler_)) {
        bgfx::destroy(bloomSampler_);
        bloomSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(sourceSampler_)) {
        bgfx::destroy(sourceSampler_);
        sourceSampler_ = BGFX_INVALID_HANDLE;
    }
    DestroyPrograms();
}

void ScenePostProcessRenderer::DestroyPrograms() noexcept {
    if (bgfx::isValid(combineProgram_)) {
        bgfx::destroy(combineProgram_);
        combineProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(blurProgram_)) {
        bgfx::destroy(blurProgram_);
        blurProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(prefilterProgram_)) {
        bgfx::destroy(prefilterProgram_);
        prefilterProgram_ = BGFX_INVALID_HANDLE;
    }
}

bgfx::TextureHandle ScenePostProcessRenderer::Submit(const ScenePostProcessSubmitDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return BGFX_INVALID_HANDLE;
    }

    const float width = static_cast<float>(std::max(1U, desc.target.extent.width));
    const float height = static_cast<float>(std::max(1U, desc.target.extent.height));
    const float bloomStrength = std::max(desc.settings.bloomStrength, 0.0F);
    const float bloomThreshold = std::max(desc.settings.bloomThreshold, 0.0F);
    const float bloomRadius = std::max(desc.settings.bloomRadiusPixels, 0.0F);

    ConfigureFullscreenView(
        desc.viewIds.postProcessBloomPrefilter,
        desc.target.bloomFrameBuffer,
        desc.target.extent,
        "KB Post Bloom Prefilter");
    const float prefilterParams[4] = {bloomThreshold, 0.0F, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, prefilterParams);
    bgfx::setTexture(0, sourceSampler_, desc.sceneColor);
    SubmitFullscreen(desc.viewIds.postProcessBloomPrefilter, prefilterProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessBloomBlurH,
        desc.target.pingFrameBuffer,
        desc.target.extent,
        "KB Post Bloom Blur H");
    const float blurHParams[4] = {(1.0F / width) * bloomRadius, 0.0F, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, blurHParams);
    bgfx::setTexture(0, sourceSampler_, desc.target.bloomTexture);
    SubmitFullscreen(desc.viewIds.postProcessBloomBlurH, blurProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessBloomBlurV,
        desc.target.bloomFrameBuffer,
        desc.target.extent,
        "KB Post Bloom Blur V");
    const float blurVParams[4] = {0.0F, (1.0F / height) * bloomRadius, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, blurVParams);
    bgfx::setTexture(0, sourceSampler_, desc.target.pingTexture);
    SubmitFullscreen(desc.viewIds.postProcessBloomBlurV, blurProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessHdrCombine,
        desc.target.combineFrameBuffer,
        desc.target.extent,
        "KB Post HDR Combine");
    const float combineParams[4] = {bloomStrength, 0.0F, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, combineParams);
    bgfx::setTexture(0, sourceSampler_, desc.sceneColor);
    bgfx::setTexture(1, bloomSampler_, desc.target.bloomTexture);
    SubmitFullscreen(desc.viewIds.postProcessHdrCombine, combineProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessHdrFinalize,
        desc.target.finalFrameBuffer,
        desc.target.extent,
        "KB Post HDR Finalize");
    const float finalizeParams[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, finalizeParams);
    bgfx::setTexture(0, sourceSampler_, desc.target.combineTexture);
    bgfx::setTexture(1, bloomSampler_, desc.target.bloomTexture);
    SubmitFullscreen(desc.viewIds.postProcessHdrFinalize, combineProgram_, fullscreenVertexBuffer_);

    return desc.target.finalTexture;
}

bool ScenePostProcessRenderer::IsInitialized() const noexcept {
    return bgfx::isValid(prefilterProgram_) && bgfx::isValid(blurProgram_) &&
           bgfx::isValid(combineProgram_) && bgfx::isValid(sourceSampler_) &&
           bgfx::isValid(bloomSampler_) && bgfx::isValid(postParams_) &&
           bgfx::isValid(fullscreenVertexBuffer_);
}

} // namespace kb::render
