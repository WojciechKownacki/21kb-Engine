#include "kb/render/particles/ParticleStripRenderer.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

bool ParticleStripRenderer::Initialize() {
    if (IsInitialized()) return true;
    geometryBuilder_.Warmup();
    const bgfx::VertexLayout layout = RenderStaticMeshVertexLayout(RenderVertexFormat::P3C3);
    vertexBuffer_ = bgfx::createDynamicVertexBuffer(kParticleStripVertexBudget, layout);
    indexBuffer_ = bgfx::createDynamicIndexBuffer(kParticleStripIndexBudget);
    program_ = ShaderLoader::LoadProgram("vs_mesh.sc", "fs_mesh.sc");
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void ParticleStripRenderer::Shutdown() noexcept {
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
    return bgfx::isValid(vertexBuffer_) && bgfx::isValid(indexBuffer_) && bgfx::isValid(program_);
}

const ParticleStripBuildResult& ParticleStripRenderer::Build(
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera) noexcept {
    lastBuild_ = geometryBuilder_.Build(snapshot, camera);
    if (!IsInitialized() || !lastBuild_.Usable() || lastBuild_.vertices.empty()) return lastBuild_;
    bgfx::update(vertexBuffer_, 0U, bgfx::copy(lastBuild_.vertices.data(),
        static_cast<std::uint32_t>(lastBuild_.vertices.size_bytes())));
    bgfx::update(indexBuffer_, 0U, bgfx::copy(lastBuild_.indices.data(),
        static_cast<std::uint32_t>(lastBuild_.indices.size_bytes())));
    return lastBuild_;
}

ParticleStripSubmitResult ParticleStripRenderer::SubmitDraw(bgfx::ViewId viewId, std::uint32_t drawIndex) noexcept {
    ParticleStripSubmitResult result{.droppedSegments = lastBuild_.droppedSegmentCount};
    if (!IsInitialized() || !lastBuild_.Usable() || drawIndex >= lastBuild_.draws.size()) return result;
    const ParticleStripDraw& draw = lastBuild_.draws[drawIndex];
    bgfx::setVertexBuffer(0U, vertexBuffer_, 0U, static_cast<std::uint32_t>(lastBuild_.vertices.size()));
    bgfx::setIndexBuffer(indexBuffer_, draw.firstIndex, draw.indexCount);
    bgfx::setState(ParticleBlendState(draw.blend, draw.depth));
    bgfx::submit(viewId, program_);
    result.succeeded = true;
    result.drawCalls = 1U;
    result.submittedIndices = draw.indexCount;
    return result;
}

} // namespace kb::render
