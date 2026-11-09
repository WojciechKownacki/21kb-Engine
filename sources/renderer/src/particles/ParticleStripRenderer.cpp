#include "kb/render/particles/ParticleStripRenderer.hpp"
#include "scene/submit/SceneMeshCoverageProbe.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/particles/ParticleRenderBatcher.hpp"

#include <array>

namespace kb::render {

bool ParticleStripRenderer::Initialize() {
    if (IsInitialized()) return true;
    geometryBuilder_.Warmup();
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    vertexBuffer_ = bgfx::createDynamicVertexBuffer(kParticleStripVertexBudget, layout);
    indexBuffer_ = bgfx::createDynamicIndexBuffer(kParticleStripIndexBudget);
    program_ = ShaderLoader::LoadProgram("vs_particle_strip.sc", "fs_particle_strip.sc");
    textureSampler_ = bgfx::createUniform("s_particleAtlas", bgfx::UniformType::Sampler);
    featureParamsUniform_ = bgfx::createUniform("u_particleFeatureParams", bgfx::UniformType::Vec4);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void ParticleStripRenderer::Shutdown() noexcept {
    if (bgfx::isValid(featureParamsUniform_)) bgfx::destroy(featureParamsUniform_);
    if (bgfx::isValid(textureSampler_)) bgfx::destroy(textureSampler_);
    featureParamsUniform_ = BGFX_INVALID_HANDLE;
    textureSampler_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(indexBuffer_)) bgfx::destroy(indexBuffer_);
    if (bgfx::isValid(vertexBuffer_)) bgfx::destroy(vertexBuffer_);
    program_ = BGFX_INVALID_HANDLE;
    indexBuffer_ = BGFX_INVALID_HANDLE;
    vertexBuffer_ = BGFX_INVALID_HANDLE;
}

void ParticleStripRenderer::Warmup() { geometryBuilder_.Warmup(); }
void ParticleStripRenderer::ReleaseScene(std::uint64_t sceneId) noexcept { geometryBuilder_.ReleaseScene(sceneId); }
void ParticleStripRenderer::ReleaseAllScenes() noexcept { geometryBuilder_.ReleaseAllScenes(); }

bool ParticleStripRenderer::IsInitialized() const noexcept {
    return bgfx::isValid(vertexBuffer_) && bgfx::isValid(indexBuffer_) && bgfx::isValid(program_) &&
        bgfx::isValid(textureSampler_) && bgfx::isValid(featureParamsUniform_);
}

const ParticleStripBuildResult& ParticleStripRenderer::Build(
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera,
    kb::math::Vec3 renderOffset) noexcept {
    lastBuild_ = geometryBuilder_.Build(snapshot, camera, renderOffset);
    if (!IsInitialized() || !lastBuild_.Usable() || lastBuild_.vertices.empty()) return lastBuild_;
    bgfx::update(vertexBuffer_, 0U, bgfx::copy(lastBuild_.vertices.data(),
        static_cast<std::uint32_t>(lastBuild_.vertices.size_bytes())));
    bgfx::update(indexBuffer_, 0U, bgfx::copy(lastBuild_.indices.data(),
        static_cast<std::uint32_t>(lastBuild_.indices.size_bytes())));
    return lastBuild_;
}

ParticleStripSubmitResult ParticleStripRenderer::SubmitDraw(
    bgfx::ViewId viewId, std::uint32_t drawIndex, bgfx::TextureHandle texture) noexcept {
    ParticleStripSubmitResult result{.droppedSegments = lastBuild_.droppedSegmentCount};
    if (!IsInitialized() || !lastBuild_.Usable() || drawIndex >= lastBuild_.draws.size() ||
        !bgfx::isValid(texture)) return result;
    const ParticleStripDraw& draw = lastBuild_.draws[drawIndex];
    const std::array<float, 4> featureParams{
        0.0F, 0.0F, draw.blend == kb::particles::ParticleRenderBlendMode::Add ? 1.0F : 0.0F, 0.0F};
    bgfx::setUniform(featureParamsUniform_, featureParams.data());
    bgfx::setTexture(0U, textureSampler_, texture);
    bgfx::setVertexBuffer(0U, vertexBuffer_, 0U, static_cast<std::uint32_t>(lastBuild_.vertices.size()));
    bgfx::setIndexBuffer(indexBuffer_, draw.firstIndex, draw.indexCount);
    SubmitWithSceneCoverage(coverageProbe_, viewId, program_, ParticleBlendState(draw.blend, draw.depth), true);
    result.succeeded = true;
    result.drawCalls = 1U;
    result.submittedIndices = draw.indexCount;
    return result;
}

} // namespace kb::render
