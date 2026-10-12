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

} // namespace

ScenePostProcessRenderer::~ScenePostProcessRenderer() {
    Shutdown();
}

bool ScenePostProcessSubmitDesc::IsValid() const noexcept {
    return bgfx::isValid(sceneColor) && target.IsValid() && viewIds.IsValid();
}

bool SceneTemporalReprojectionDesc::IsValid() const noexcept {
    return bgfx::isValid(depthTexture);
}

bool ScenePostProcessRenderer::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    prefilterProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_prefilter.sc");
    blurProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_blur.sc");
    combineProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_combine.sc");
    motionVectorsProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_motion_vectors.sc");
    taaResolveProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_taa_resolve.sc");
    sourceSampler_ = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);
    bloomSampler_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
    historySampler_ = bgfx::createUniform("s_history", bgfx::UniformType::Sampler);
    velocitySampler_ = bgfx::createUniform("s_velocity", bgfx::UniformType::Sampler);
    postParams_ = bgfx::createUniform("u_postParams", bgfx::UniformType::Vec4);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_inverseViewProjection", bgfx::UniformType::Mat4);
    previousViewProjectionUniform_ = bgfx::createUniform("u_previousViewProjection", bgfx::UniformType::Mat4);
    temporalParamsUniform_ = bgfx::createUniform("u_temporalParams", bgfx::UniformType::Vec4);
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
    if (bgfx::isValid(temporalParamsUniform_)) {
        bgfx::destroy(temporalParamsUniform_);
        temporalParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(previousViewProjectionUniform_)) {
        bgfx::destroy(previousViewProjectionUniform_);
        previousViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(inverseViewProjectionUniform_)) {
        bgfx::destroy(inverseViewProjectionUniform_);
        inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(velocitySampler_)) {
        bgfx::destroy(velocitySampler_);
        velocitySampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(historySampler_)) {
        bgfx::destroy(historySampler_);
        historySampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(depthSampler_)) {
        bgfx::destroy(depthSampler_);
        depthSampler_ = BGFX_INVALID_HANDLE;
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
    if (bgfx::isValid(taaResolveProgram_)) {
        bgfx::destroy(taaResolveProgram_);
        taaResolveProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(motionVectorsProgram_)) {
        bgfx::destroy(motionVectorsProgram_);
        motionVectorsProgram_ = BGFX_INVALID_HANDLE;
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

bool ScenePostProcessRenderer::IsInitialized() const noexcept {
    return bgfx::isValid(prefilterProgram_) && bgfx::isValid(blurProgram_) &&
           bgfx::isValid(combineProgram_) && bgfx::isValid(motionVectorsProgram_) &&
           bgfx::isValid(taaResolveProgram_) && bgfx::isValid(sourceSampler_) &&
           bgfx::isValid(bloomSampler_) && bgfx::isValid(depthSampler_) &&
           bgfx::isValid(historySampler_) && bgfx::isValid(velocitySampler_) &&
           bgfx::isValid(postParams_) && bgfx::isValid(inverseViewProjectionUniform_) &&
           bgfx::isValid(previousViewProjectionUniform_) && bgfx::isValid(temporalParamsUniform_) &&
           bgfx::isValid(fullscreenVertexBuffer_);
}

} // namespace kb::render
