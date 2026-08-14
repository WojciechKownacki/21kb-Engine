#include "kb/render/particles/ParticleGpuRenderer.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/resources/BuiltInParticleQuadMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] float DecodeSnorm(std::int16_t value) noexcept {
    return std::max(-1.0F, static_cast<float>(value) / 32'767.0F);
}

} // namespace

bool ParticleGpuRenderer::Initialize() {
    if (IsInitialized()) return true;
    program_ = ShaderLoader::LoadProgram("vs_particle_instanced.sc", "fs_particle_instanced.sc");
    atlasSampler_ = bgfx::createUniform("s_particleAtlas", bgfx::UniformType::Sampler);
    sceneDepthSampler_ = bgfx::createUniform("s_particleSceneDepth", bgfx::UniformType::Sampler);
    cameraBasisUniform_ = bgfx::createUniform("u_particleCameraBasis", bgfx::UniformType::Vec4, 3U);
    emitterParamsUniform_ = bgfx::createUniform("u_particleEmitterParams", bgfx::UniformType::Vec4);
    featureParamsUniform_ = bgfx::createUniform("u_particleFeatureParams", bgfx::UniformType::Vec4);
    localBasisUniform_ = bgfx::createUniform("u_particleLocalBasis", bgfx::UniformType::Vec4);
    depthParamsUniform_ = bgfx::createUniform("u_particleDepthParams", bgfx::UniformType::Vec4);
    constexpr std::uint32_t white = 0xFFFFFFFFU;
    whiteTexture_ = bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(&white, sizeof(white)));
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    Warmup(static_cast<std::uint32_t>(kb::scene::kParticleEffectMaxCpuParticlesPerScene));
    return true;
}

void ParticleGpuRenderer::Shutdown() noexcept {
    if (bgfx::isValid(whiteTexture_)) bgfx::destroy(whiteTexture_);
    if (bgfx::isValid(depthParamsUniform_)) bgfx::destroy(depthParamsUniform_);
    if (bgfx::isValid(localBasisUniform_)) bgfx::destroy(localBasisUniform_);
    if (bgfx::isValid(featureParamsUniform_)) bgfx::destroy(featureParamsUniform_);
    if (bgfx::isValid(emitterParamsUniform_)) bgfx::destroy(emitterParamsUniform_);
    if (bgfx::isValid(cameraBasisUniform_)) bgfx::destroy(cameraBasisUniform_);
    if (bgfx::isValid(sceneDepthSampler_)) bgfx::destroy(sceneDepthSampler_);
    if (bgfx::isValid(atlasSampler_)) bgfx::destroy(atlasSampler_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    whiteTexture_ = BGFX_INVALID_HANDLE;
    depthParamsUniform_ = BGFX_INVALID_HANDLE;
    localBasisUniform_ = BGFX_INVALID_HANDLE;
    featureParamsUniform_ = BGFX_INVALID_HANDLE;
    emitterParamsUniform_ = BGFX_INVALID_HANDLE;
    cameraBasisUniform_ = BGFX_INVALID_HANDLE;
    sceneDepthSampler_ = BGFX_INVALID_HANDLE;
    atlasSampler_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
}

void ParticleGpuRenderer::Warmup(std::uint32_t particleCapacity) { batcher_.Warmup(particleCapacity); }

bool ParticleGpuRenderer::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(atlasSampler_) && bgfx::isValid(sceneDepthSampler_) &&
        bgfx::isValid(cameraBasisUniform_) && bgfx::isValid(emitterParamsUniform_) &&
        bgfx::isValid(featureParamsUniform_) && bgfx::isValid(localBasisUniform_) &&
        bgfx::isValid(depthParamsUniform_) &&
        bgfx::isValid(whiteTexture_);
}

ParticleGpuSubmitResult ParticleGpuRenderer::Submit(
    bgfx::ViewId viewId,
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle sceneDepthTexture) noexcept {
    static_cast<void>(Build(snapshot, camera));
    ParticleGpuSubmitResult result{
        .droppedParticles = lastBuild_.droppedParticleCount,
        .status = lastBuild_.status,
    };
    if (!IsInitialized() || !lastBuild_.Succeeded()) return result;

    result.succeeded = true;
    for (std::uint32_t batchIndex = 0U; batchIndex < lastBuild_.batches.size(); ++batchIndex) {
        const ParticleGpuSubmitResult batchResult = SubmitBatch(
            viewId, batchIndex, snapshot, camera, resources, resourceMap, sceneDepthTexture);
        result.succeeded = result.succeeded && batchResult.succeeded;
        result.drawCalls += batchResult.drawCalls;
        result.submittedParticles += batchResult.submittedParticles;
        result.droppedParticles += batchResult.droppedParticles;
    }
    return result;
}

const ParticleRenderBatchBuildResult& ParticleGpuRenderer::Build(
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera) noexcept {
    lastBuild_ = batcher_.Build(snapshot, camera);
    return lastBuild_;
}

ParticleGpuSubmitResult ParticleGpuRenderer::SubmitBatch(
    bgfx::ViewId viewId,
    std::uint32_t batchIndex,
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle sceneDepthTexture) noexcept {
    ParticleGpuSubmitResult result{.status = lastBuild_.status};
    if (!IsInitialized() || !lastBuild_.Succeeded() || batchIndex >= lastBuild_.batches.size()) return result;

    const RenderMeshHandle quadHandle = resourceMap.ResolveMesh(BuiltInParticleQuadMeshAssetId().value);
    const RenderMeshResource* quad = resources.FindMesh(quadHandle);
    if (quad == nullptr || !bgfx::isValid(quad->vertexBuffer) || !bgfx::isValid(quad->indexBuffer)) return result;

    const std::array<float, 12> cameraBasis{
        camera.view[0], camera.view[4], camera.view[8], 0.0F,
        camera.view[1], camera.view[5], camera.view[9], 0.0F,
        camera.view[2], camera.view[6], camera.view[10], 0.0F,
    };
    bgfx::setUniform(cameraBasisUniform_, cameraBasis.data(), 3U);
    const ParticleRenderBatch& batch = lastBuild_.batches[batchIndex];
        const auto& emitter = snapshot.Emitters()[batch.emitterRecordIndex];
        const std::uint32_t available = bgfx::getAvailInstanceDataBuffer(
            batch.instanceCount, static_cast<std::uint16_t>(sizeof(ParticleGpuInstance)));
        if (available == 0U) {
            result.droppedParticles += batch.instanceCount;
            return result;
        }
        bgfx::InstanceDataBuffer buffer{};
        bgfx::allocInstanceDataBuffer(&buffer, available, static_cast<std::uint16_t>(sizeof(ParticleGpuInstance)));
        const auto source = lastBuild_.instances.subspan(batch.firstInstance, available);
        std::copy(source.begin(), source.end(), reinterpret_cast<ParticleGpuInstance*>(buffer.data));
        bgfx::setInstanceDataBuffer(&buffer, 0U, available);
        bgfx::setVertexBuffer(0U, quad->vertexBuffer);
        bgfx::setIndexBuffer(quad->indexBuffer);

        const std::array<float, 4> emitterParams{
            static_cast<float>(emitter.FlipbookColumns()), static_cast<float>(emitter.FlipbookRows()),
            static_cast<float>(emitter.alignment), emitter.pointSpriteDiameter};
        const std::array<float, 4> localBasis{
            DecodeSnorm(emitter.localBasisQuaternionSnorm[0]),
            DecodeSnorm(emitter.localBasisQuaternionSnorm[1]),
            DecodeSnorm(emitter.localBasisQuaternionSnorm[2]),
            DecodeSnorm(emitter.localBasisQuaternionSnorm[3])};
        const std::array<float, 4> depthParams{
            camera.projection[10], camera.projection[14], kParticleSoftFadeDistanceMeters,
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::SoftParticles) &&
                bgfx::isValid(sceneDepthTexture) ? 1.0F : 0.0F};
        const std::array<float, 4> featureParams{
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::AntiAliasing) ? 1.0F : 0.0F,
            static_cast<float>(emitter.output), 0.0F, 0.0F};
        bgfx::setUniform(emitterParamsUniform_, emitterParams.data());
        bgfx::setUniform(featureParamsUniform_, featureParams.data());
        bgfx::setUniform(localBasisUniform_, localBasis.data());
        bgfx::setUniform(depthParamsUniform_, depthParams.data());

        bgfx::TextureHandle atlas = whiteTexture_;
        if (emitter.textureAtlasAssetId != 0U) {
            const RenderTextureHandle atlasHandle = resourceMap.ResolveTexture(
                emitter.textureAtlasAssetId, RenderTextureColorSpace::Srgb);
            if (const RenderTextureResource* texture = resources.FindTexture(atlasHandle);
                texture != nullptr && bgfx::isValid(texture->texture)) atlas = texture->texture;
        }
        bgfx::setTexture(0U, atlasSampler_, atlas);
        bgfx::setTexture(1U, sceneDepthSampler_, bgfx::isValid(sceneDepthTexture) ? sceneDepthTexture : whiteTexture_);
        bgfx::setState(ParticleBlendState(batch.blend, batch.depth));
        bgfx::submit(viewId, program_);
        result.submittedParticles += available;
        result.droppedParticles += batch.instanceCount - available;
        ++result.drawCalls;
    result.succeeded = true;
    return result;
}

const ParticleRenderBatchBuildResult& ParticleGpuRenderer::LastBuild() const noexcept { return lastBuild_; }

} // namespace kb::render
