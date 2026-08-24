#include "kb/render/particles/ParticleGpuRenderer.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/resources/BuiltInParticleQuadMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] float DecodeSnorm(std::int16_t value) noexcept {
    return std::max(-1.0F, static_cast<float>(value) / 32'767.0F);
}

enum class ParticleSpriteKind : std::uint8_t { EnergyGlow, VelocityStreak };

[[nodiscard]] float HashNoise(float x, float y) noexcept {
    const float n = std::sin(x * 127.1F + y * 311.7F) * 43758.5453F;
    return n - std::floor(n);
}

void WriteParticleSprite(
    std::vector<std::uint8_t>& pixels, std::uint16_t width, std::uint16_t height, ParticleSpriteKind kind) {
    pixels.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
    for (std::uint16_t y = 0U; y < height; ++y) {
        for (std::uint16_t x = 0U; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
            const float v = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);
            const float dx = u * 2.0F - 1.0F;
            const float dy = v * 2.0F - 1.0F;
            const float radiusSquared = dx * dx + dy * dy;
            const float radius = std::sqrt(radiusSquared);
            float energy = 0.0F;
            if (kind == ParticleSpriteKind::EnergyGlow) {
                if (radiusSquared < 1.0F) {
                    const float core = std::exp(-radiusSquared * 22.0F);
                    const float bloom = std::exp(-radiusSquared * 5.2F);
                    const float halo = std::exp(-radiusSquared * 1.35F) * std::max(0.0F, 1.0F - radius);
                    const float arm = std::exp(-std::min(dx * dx, dy * dy) * 90.0F) *
                        std::exp(-radiusSquared * 3.4F);
                    const float grain = 0.04F * HashNoise(u * 17.0F, v * 19.0F) * halo;
                    energy = core * 1.45F + bloom * 0.62F + halo * 0.28F + arm * 0.22F + grain;
                }
            } else if (radiusSquared < 1.35F) {
                const float line = std::exp(-dx * dx * 36.0F) * std::max(0.0F, 1.0F - dy * dy);
                const float glow = std::exp(-(dx * dx * 14.0F + dy * dy * 2.4F));
                const float tip = std::exp(-dx * dx * 8.0F) * std::exp(-std::abs(dy) * 4.5F);
                energy = line * 1.25F + glow * 0.7F + tip * 0.35F;
            }
            energy = std::clamp(energy, 0.0F, 1.0F);
            const std::size_t pixel = (static_cast<std::size_t>(y) * width + x) * 4U;
            const std::uint8_t byte = static_cast<std::uint8_t>(energy * 255.0F + 0.5F);
            pixels[pixel] = byte;
            pixels[pixel + 1U] = byte;
            pixels[pixel + 2U] = byte;
            pixels[pixel + 3U] = byte;
        }
    }
}

[[nodiscard]] bgfx::TextureHandle CreateParticleSprite(std::uint16_t width, std::uint16_t height, ParticleSpriteKind kind) {
    std::vector<std::uint8_t> pixels;
    WriteParticleSprite(pixels, width, height, kind);
    constexpr std::uint64_t kSampler =
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    return bgfx::createTexture2D(width, height, false, 1U, bgfx::TextureFormat::RGBA8, kSampler,
        bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size())));
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
    volumetricParamsUniform_ = bgfx::createUniform("u_particleVolumetricParams", bgfx::UniformType::Vec4);
    whiteTexture_ = CreateParticleSprite(256U, 256U, ParticleSpriteKind::EnergyGlow);
    streakTexture_ = CreateParticleSprite(256U, 256U, ParticleSpriteKind::VelocityStreak);
    if (!stripRenderer_.Initialize()) {
        Shutdown();
        return false;
    }
    static_cast<void>(visualSimulation_.Initialize());
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    Warmup(4'096U);
    return true;
}

void ParticleGpuRenderer::Shutdown() noexcept {
    visualSimulation_.Shutdown();
    stripRenderer_.Shutdown();
    if (bgfx::isValid(streakTexture_)) bgfx::destroy(streakTexture_);
    if (bgfx::isValid(whiteTexture_)) bgfx::destroy(whiteTexture_);
    if (bgfx::isValid(depthParamsUniform_)) bgfx::destroy(depthParamsUniform_);
    if (bgfx::isValid(volumetricParamsUniform_)) bgfx::destroy(volumetricParamsUniform_);
    if (bgfx::isValid(localBasisUniform_)) bgfx::destroy(localBasisUniform_);
    if (bgfx::isValid(featureParamsUniform_)) bgfx::destroy(featureParamsUniform_);
    if (bgfx::isValid(emitterParamsUniform_)) bgfx::destroy(emitterParamsUniform_);
    if (bgfx::isValid(cameraBasisUniform_)) bgfx::destroy(cameraBasisUniform_);
    if (bgfx::isValid(sceneDepthSampler_)) bgfx::destroy(sceneDepthSampler_);
    if (bgfx::isValid(atlasSampler_)) bgfx::destroy(atlasSampler_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    streakTexture_ = BGFX_INVALID_HANDLE;
    whiteTexture_ = BGFX_INVALID_HANDLE;
    depthParamsUniform_ = BGFX_INVALID_HANDLE;
    volumetricParamsUniform_ = BGFX_INVALID_HANDLE;
    localBasisUniform_ = BGFX_INVALID_HANDLE;
    featureParamsUniform_ = BGFX_INVALID_HANDLE;
    emitterParamsUniform_ = BGFX_INVALID_HANDLE;
    cameraBasisUniform_ = BGFX_INVALID_HANDLE;
    sceneDepthSampler_ = BGFX_INVALID_HANDLE;
    atlasSampler_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
}

void ParticleGpuRenderer::Warmup(std::uint32_t particleCapacity) {
    batcher_.Warmup(particleCapacity);
    visualMaskScratch_.reserve(particleCapacity);
}

bool ParticleGpuRenderer::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(atlasSampler_) && bgfx::isValid(sceneDepthSampler_) &&
        bgfx::isValid(cameraBasisUniform_) && bgfx::isValid(emitterParamsUniform_) &&
        bgfx::isValid(featureParamsUniform_) && bgfx::isValid(localBasisUniform_) &&
        bgfx::isValid(depthParamsUniform_) && bgfx::isValid(volumetricParamsUniform_) &&
        bgfx::isValid(whiteTexture_) && bgfx::isValid(streakTexture_) && stripRenderer_.IsInitialized();
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
        result.submittedVolumetricParticles += batchResult.submittedVolumetricParticles;
        result.volumetricRaymarchSteps += batchResult.volumetricRaymarchSteps;
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

kb::particles::ParticleGpuVisualAvailability ParticleGpuRenderer::GpuVisualAvailability() const noexcept {
    return visualSimulation_.Availability();
}

void ParticleGpuRenderer::SetVolumetricQuality(ParticleVolumetricQuality quality) noexcept {
    volumetricQuality_ = quality;
}

ParticleVolumetricQuality ParticleGpuRenderer::VolumetricQuality() const noexcept {
    return volumetricQuality_;
}

bool ParticleGpuRenderer::PrepareVisualSimulation(
    bgfx::ViewId viewId,
    const kb::particles::ParticleRenderSnapshot& snapshot) noexcept {
    if (!lastBuild_.Succeeded()) return false;
    visualMaskScratch_.assign(lastBuild_.instances.size(), 0U);
    bool hasVisualEmitter = false;
    for (const ParticleRenderBatch& batch : lastBuild_.batches) {
        const kb::particles::ParticleRenderEmitterRecord& emitter =
        snapshot.Emitters()[batch.emitterRecordIndex];
        if (emitter.backendPolicy == kb::scene::ParticleBackendPolicy::CpuDeterministic) continue;
        hasVisualEmitter = true;
        const std::uint32_t end = batch.firstInstance + batch.instanceCount;
        if (end > visualMaskScratch_.size()) return false;
        std::fill(visualMaskScratch_.begin() + batch.firstInstance, visualMaskScratch_.begin() + end, 1U);
    }
    if (!hasVisualEmitter) return false;
    return visualSimulation_.Prepare(
        viewId, snapshot.SceneId(), snapshot.FixedStepIndex(), lastBuild_.instances, visualMaskScratch_);
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
        if (emitter.output == kb::particles::ParticleRenderOutput::Volumetric &&
            !bgfx::isValid(sceneDepthTexture)) return result;
        const bool useVisualSimulation = visualSimulation_.HasPreparedView(
            viewId, snapshot.SceneId(), snapshot.FixedStepIndex());
        std::uint32_t available = batch.instanceCount;
        if (useVisualSimulation) {
            bgfx::setInstanceDataBuffer(visualSimulation_.InstanceBuffer(), batch.firstInstance, available);
        } else {
            available = bgfx::getAvailInstanceDataBuffer(
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
        }
        bgfx::setVertexBuffer(0U, quad->vertexBuffer);
        bgfx::setIndexBuffer(quad->indexBuffer);

        const std::array<float, 4> emitterParams{
            static_cast<float>(emitter.FlipbookColumns()), static_cast<float>(emitter.FlipbookRows()),
            static_cast<float>(emitter.alignment),
            emitter.output == kb::particles::ParticleRenderOutput::Volumetric
                ? emitter.volumetricRadiusScale : emitter.pointSpriteDiameter};
        const std::array<float, 4> localBasis{
            DecodeSnorm(emitter.localBasisQuaternionSnorm[0]),
            DecodeSnorm(emitter.localBasisQuaternionSnorm[1]),
            DecodeSnorm(emitter.localBasisQuaternionSnorm[2]),
            DecodeSnorm(emitter.localBasisQuaternionSnorm[3])};
        const std::array<float, 4> depthParams{
            camera.projection[10], camera.projection[14], kParticleSoftFadeDistanceMeters,
            (kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::SoftParticles) ||
                emitter.output == kb::particles::ParticleRenderOutput::Volumetric) &&
                bgfx::isValid(sceneDepthTexture) ? 1.0F : 0.0F};
        const std::array<float, 4> featureParams{
            kb::particles::HasParticleRenderEmitterFlag(
                emitter.flags, kb::particles::ParticleRenderEmitterFlag::AntiAliasing) ? 1.0F : 0.0F,
            static_cast<float>(emitter.output),
            emitter.blend == kb::particles::ParticleRenderBlendMode::Add ? 1.0F : 0.0F, 0.0F};
        bgfx::setUniform(emitterParamsUniform_, emitterParams.data());
        bgfx::setUniform(featureParamsUniform_, featureParams.data());
        const std::uint32_t volumetricSteps = volumetricQuality_ == ParticleVolumetricQuality::Low
            ? emitter.volumetricLowQualitySteps : emitter.volumetricHighQualitySteps;
        const std::array<float, 4> volumetricParams{
            emitter.volumetricDensity, emitter.volumetricRadiusScale,
            static_cast<float>(volumetricSteps), 0.0F};
        bgfx::setUniform(volumetricParamsUniform_, volumetricParams.data());
        bgfx::setUniform(localBasisUniform_, localBasis.data());
        bgfx::setUniform(depthParamsUniform_, depthParams.data());

        bgfx::TextureHandle atlas =
            emitter.output == kb::particles::ParticleRenderOutput::StretchedBillboard &&
                bgfx::isValid(streakTexture_)
            ? streakTexture_ : whiteTexture_;
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
        if (emitter.output == kb::particles::ParticleRenderOutput::Volumetric) {
            result.submittedVolumetricParticles += available;
            result.volumetricRaymarchSteps += static_cast<std::uint64_t>(available) * volumetricSteps;
        }
        result.droppedParticles += batch.instanceCount - available;
        ++result.drawCalls;
    result.succeeded = true;
    return result;
}

const ParticleRenderBatchBuildResult& ParticleGpuRenderer::LastBuild() const noexcept { return lastBuild_; }

const ParticleStripBuildResult& ParticleGpuRenderer::BuildStrips(
    const kb::particles::ParticleRenderSnapshot& snapshot,
    const SceneRenderCamera& camera) noexcept {
    return stripRenderer_.Build(snapshot, camera);
}

ParticleStripSubmitResult ParticleGpuRenderer::SubmitStripDraw(bgfx::ViewId viewId, std::uint32_t drawIndex) noexcept {
    return stripRenderer_.SubmitDraw(viewId, drawIndex);
}

void ParticleGpuRenderer::ReleaseParticleScene(std::uint64_t sceneId) noexcept {
    visualSimulation_.ReleaseScene(sceneId);
    stripRenderer_.ReleaseScene(sceneId);
}

void ParticleGpuRenderer::ReleaseAllParticleScenes() noexcept {
    visualSimulation_.ReleaseAllScenes();
    stripRenderer_.ReleaseAllScenes();
}

} // namespace kb::render
