#include "scene/SceneMeshSubmitter.hpp"

#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "kb/render/particles/ParticleGpuRenderer.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"
#include "scene/submit/SceneMeshDrawCommandSubmitter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {
namespace {

void BuildVisibilityBlockerInputs(const RenderScene& scene, std::vector<SceneRenderVisibilityBlocker>& out) {
    out.clear();
    out.reserve(scene.VisibilityBlockerProxies().size());
    for (const auto& [entityId, proxy] : scene.VisibilityBlockerProxies()) {
        const VisibilityBlockerRenderProxyDesc& source = proxy.desc;
        out.push_back(SceneRenderVisibilityBlocker{ .entityId = entityId, .model = source.model, .localCenter = source.localCenter, .size = source.size });
    }
}

[[nodiscard]] std::uint32_t ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept {
    switch (filter) {
    case SceneRenderShadowFilter::Hard:
        return 1U;
    case SceneRenderShadowFilter::Pcf3x3:
        return 9U;
    case SceneRenderShadowFilter::Evsm:
        return 4U;
    case SceneRenderShadowFilter::Msm:
        return 4U;
    case SceneRenderShadowFilter::Pcss:
        return 16U;
    }
    return 9U;
}

} // namespace

bool SceneMeshSubmitter::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    if (!passResources_.Initialize() || !gpuDrivenCullingPass_.Initialize()) {
        Shutdown();
        return false;
    }
    transparentSubmissionScratch_.reserve(4'096U + kParticleGpuMaxBatches);
    meshBatchSubmissionScratch_.reserve(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
    particleMeshBatchBuilder_.Warmup(kb::scene::kParticleEffectMaxCpuParticlesPerScene);

    return true;
}

void SceneMeshSubmitter::Shutdown() {
    gpuDrivenFrameResources_.Shutdown();
    gpuDrivenCullingPass_.Shutdown();
    passResources_.Shutdown();
    pipelineScratch_.detailSwitchLevels.clear();
    pipelineScratch_.detailSwitchPreviousLevels.clear();
    detailSwitchScene_ = nullptr;
}

SceneRenderSubmitStats SceneMeshSubmitter::ValidateResourcesInto(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    MeshPipelineBuildResult& pipelineScratch,
    MeshPassType pass,
    const SceneRenderCamera* camera,
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDrawBudget drawBudget,
    SceneRenderLightingConfig lightingConfig,
    std::span<const std::uint64_t> selectedEntityIds,
    SceneGpuDrivenFeatureSupport gpuDrivenSupport,
    bool terrainLayersOnly) noexcept {
    const std::vector<SceneRenderDrawGroup>& drawGroups = renderScene.DrawGroups();
    std::vector<SceneRenderVisibilityBlocker> visibilityBlockers;
    BuildVisibilityBlockerInputs(renderScene, visibilityBlockers);
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = pass,
        .drawGroups = &drawGroups,
        .resources = &resources,
        .resourceMap = &resourceMap,
        .camera = camera,
        .visibilityBlockers = visibilityBlockers,
        .diagnostics = diagnostics,
        .maxDrawCommands = drawBudget.maxDrawCommands,
        .maxVisibleInstances = drawBudget.maxVisibleInstances,
        .maxDroppedInstances = drawBudget.maxDroppedInstances,
        .selectedEntityIds = selectedEntityIds,
        .gpuDrivenSupport = gpuDrivenSupport,
        .terrainLayersOnly = terrainLayersOnly,
    }, pipelineScratch);
    pipelineScratch.stats.meshDrawGroupScratchCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupCapacity());
    pipelineScratch.stats.meshDrawGroupInstanceScratchCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupInstanceCapacity());
    pipelineScratch.stats.meshDrawGroupLookupCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupLookupScratchCapacity());
    SceneRenderSubmitStats lightingStats{};
    static_cast<void>(SceneLightingPacker::Build(renderScene, lightingStats, lightingConfig, camera));
    pipelineScratch.stats.sceneLightCount = lightingStats.sceneLightCount;
    pipelineScratch.stats.submittedForwardLightCount = lightingStats.submittedForwardLightCount;
    pipelineScratch.stats.skippedForwardLightCount = lightingStats.skippedForwardLightCount;
    pipelineScratch.stats.invalidLightCount = lightingStats.invalidLightCount;
    pipelineScratch.stats.forwardLightCapacity = lightingStats.forwardLightCapacity;
    pipelineScratch.stats.lightingPath = lightingStats.lightingPath;
    pipelineScratch.stats.lightingPathProduction = lightingStats.lightingPathProduction;
    pipelineScratch.stats.lightClusterCount = lightingStats.lightClusterCount;
    pipelineScratch.stats.submittedAreaLightCount = lightingStats.submittedAreaLightCount;
    pipelineScratch.stats.submittedVolumetricLightCount = lightingStats.submittedVolumetricLightCount;
    pipelineScratch.stats.contactShadowLightCount = lightingStats.contactShadowLightCount;
    pipelineScratch.stats.submittedEnvironmentLightingCount = lightingStats.submittedEnvironmentLightingCount;
    pipelineScratch.stats.environmentLightingMode = lightingStats.environmentLightingMode;
    pipelineScratch.stats.environmentLightingSampleCount = lightingStats.environmentLightingSampleCount;
    pipelineScratch.stats.reflectionProbeCount = lightingStats.reflectionProbeCount;
    pipelineScratch.stats.localReflectionProbeCount = lightingStats.localReflectionProbeCount;
    pipelineScratch.stats.parallaxCorrectedProbeCount = lightingStats.parallaxCorrectedProbeCount;
    pipelineScratch.stats.globalIlluminationMode = lightingStats.globalIlluminationMode;
    if (pass == MeshPassType::ShadowDepth) {
        pipelineScratch.stats.shadowCasterCount = pipelineScratch.stats.visibleMeshCount;
    }
    MeshPipelineProcessor::CountCommandsAsSubmitted(pipelineScratch.stats, pipelineScratch.commands);
    if (pass == MeshPassType::ShadowDepth) {
        pipelineScratch.stats.submittedShadowCasterCount = pipelineScratch.stats.submittedMeshCount;
        pipelineScratch.stats.submittedShadowDrawCallCount = pipelineScratch.stats.submittedDrawCallCount;
        pipelineScratch.stats.shadowMapSize = lightingConfig.shadowMapSize;
        pipelineScratch.stats.shadowFilterSampleCount = ShadowFilterSampleCount(lightingConfig.shadowFilter);
    }
    return pipelineScratch.stats;
}

SceneRenderSubmitStats SceneMeshSubmitter::Submit(
    bgfx::ViewId viewId,
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    MeshPassType pass,
    const SceneRenderCamera* camera,
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDrawBudget drawBudget,
    SceneRenderLightingConfig lightingConfig,
    const SceneRenderShadowMapBinding* shadowMap,
    std::span<const std::uint64_t> selectedEntityIds,
    SceneGpuDrivenFeatureSupport gpuDrivenSupport,
    std::array<float, 4> frameTime,
    std::array<float, 4> dynamicParameter,
    bgfx::TextureHandle sceneDepthTexture,
    bgfx::TextureHandle sceneColorTexture,
    bool terrainLayersOnly,
    std::array<float, 16> motionVectorPreviousViewProjection,
    ParticleGpuRenderer* particleRenderer,
    const kb::particles::ParticleRenderSnapshot* particleSnapshot) const {
    SceneRenderSubmitStats stats{};
    if (!IsInitialized()) {
        return stats;
    }

    const std::vector<SceneRenderDrawGroup>& drawGroups = renderScene.DrawGroups();
    std::vector<SceneRenderVisibilityBlocker> visibilityBlockers;
    BuildVisibilityBlockerInputs(renderScene, visibilityBlockers);
    SceneMeshBatchBuilder::BuildInto(drawGroups, meshBatchSubmissionScratch_);
    if (particleSnapshot != nullptr) {
        particleMeshBatchBuilder_.Build(*particleSnapshot);
        const auto& particleMeshBatches = particleMeshBatchBuilder_.Batches();
        meshBatchSubmissionScratch_.insert(
            meshBatchSubmissionScratch_.end(), particleMeshBatches.begin(), particleMeshBatches.end());
    }
    SceneRenderSubmitStats lightingStats{};
    const PackedSceneLighting lighting = SceneLightingPacker::Build(renderScene, lightingStats, lightingConfig, camera);
    const std::array<float, 4> cameraPosition = SceneLightingPacker::CameraPosition(camera);
    if (detailSwitchScene_ != &renderScene) {
        pipelineScratch_.detailSwitchLevels.clear();
        pipelineScratch_.detailSwitchPreviousLevels.clear();
        detailSwitchScene_ = &renderScene;
    }
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = pass,
        .meshBatches = &meshBatchSubmissionScratch_,
        .resources = &resources,
        .resourceMap = &resourceMap,
        .camera = camera,
        .visibilityBlockers = visibilityBlockers,
        .diagnostics = diagnostics,
        .maxDrawCommands = drawBudget.maxDrawCommands,
        .maxVisibleInstances = drawBudget.maxVisibleInstances,
        .maxDroppedInstances = drawBudget.maxDroppedInstances,
        .selectedEntityIds = selectedEntityIds,
        .gpuDrivenSupport = gpuDrivenSupport,
        .terrainLayersOnly = terrainLayersOnly,
    }, pipelineScratch_);
    stats = pipelineScratch_.stats;
    stats.sceneLightCount = lightingStats.sceneLightCount;
    stats.submittedForwardLightCount = lightingStats.submittedForwardLightCount;
    stats.skippedForwardLightCount = lightingStats.skippedForwardLightCount;
    stats.invalidLightCount = lightingStats.invalidLightCount;
    stats.forwardLightCapacity = lightingStats.forwardLightCapacity;
    stats.lightingPath = lightingStats.lightingPath;
    stats.lightingPathProduction = lightingStats.lightingPathProduction;
    stats.lightClusterCount = lightingStats.lightClusterCount;
    stats.submittedAreaLightCount = lightingStats.submittedAreaLightCount;
    stats.submittedVolumetricLightCount = lightingStats.submittedVolumetricLightCount;
    stats.contactShadowLightCount = lightingStats.contactShadowLightCount;
    stats.submittedEnvironmentLightingCount = lightingStats.submittedEnvironmentLightingCount;
    stats.environmentLightingMode = lightingStats.environmentLightingMode;
    stats.environmentLightingSampleCount = lightingStats.environmentLightingSampleCount;
    stats.reflectionProbeCount = lightingStats.reflectionProbeCount;
    stats.localReflectionProbeCount = lightingStats.localReflectionProbeCount;
    stats.parallaxCorrectedProbeCount = lightingStats.parallaxCorrectedProbeCount;
    stats.globalIlluminationMode = lightingStats.globalIlluminationMode;
    if (pass == MeshPassType::ShadowDepth) {
        stats.shadowCasterCount = stats.visibleMeshCount;
        stats.shadowMapSize = lightingConfig.shadowMapSize;
        stats.shadowFilterSampleCount = ShadowFilterSampleCount(lightingConfig.shadowFilter);
    }
    stats.meshDrawGroupScratchCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupCapacity());
    stats.meshDrawGroupInstanceScratchCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupInstanceCapacity());
    stats.meshDrawGroupLookupCapacity = static_cast<std::uint32_t>(renderScene.DrawGroupLookupScratchCapacity());
    if (stats.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::ComputeCulling ||
        stats.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::IndirectDrawSubmit ||
        stats.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::MeshletSubmit) {
        const SceneGpuDrivenFrameBatch gpuDrivenBatch = gpuDrivenFrameResources_.Upload(pipelineScratch_.gpuDrivenInputRecords);
        stats += gpuDrivenCullingPass_.Submit(SceneGpuDrivenCullingPassDesc{
            .viewId = ViewId::GpuCompute,
            .batch = gpuDrivenBatch,
            .camera = camera,
            .featureState = stats.gpuDrivenFeatureState,
        });
    }
    const auto submitMeshCommands = [&](std::span<const MeshDrawCommand> commands) {
        SceneMeshDrawCommandSubmitter::Submit(SceneMeshDrawCommandSubmitDesc{
        .viewId = viewId,
        .commands = commands,
        .pass = pass,
        .resources = resources,
        .resourceMap = resourceMap,
        .lighting = lighting,
        .cameraPosition = cameraPosition,
        .frameTime = frameTime,
        .dynamicParameter = dynamicParameter,
        .shadowMap = shadowMap,
        .sceneDepthTexture = sceneDepthTexture,
        .sceneColorTexture = sceneColorTexture,
        .motionVectorPreviousViewProjection = motionVectorPreviousViewProjection,
        .skinningPaletteAllocator = skinningPaletteAllocator_,
        .passResources = passResources_,
        .diagnostics = diagnostics,
        .stats = stats,
        });
    };

    if (pass == MeshPassType::BaseTransparent && particleRenderer != nullptr && particleSnapshot != nullptr) {
        const ParticleRenderBatchBuildResult& particleBuild = particleRenderer->Build(*particleSnapshot, *camera);
        static_cast<void>(particleRenderer->PrepareVisualSimulation(viewId, *particleSnapshot));
        const ParticleStripBuildResult& stripBuild = particleRenderer->BuildStrips(*particleSnapshot, *camera);
        transparentSubmissionScratch_.clear();
        for (std::uint32_t index = 0U; index < pipelineScratch_.commands.size(); ++index) {
            const MeshDrawCommand& command = pipelineScratch_.commands[index];
            transparentSubmissionScratch_.push_back(TransparentDrawOrderEntry{
                .source = TransparentDrawSource::Mesh,
                .depthBucket = command.depthBucket,
                .sourceIndex = index,
                .stableTie = command.sortKey,
            });
        }
        for (std::uint32_t index = 0U; index < particleBuild.batches.size(); ++index) {
            const ParticleRenderBatch& batch = particleBuild.batches[index];
            transparentSubmissionScratch_.push_back(TransparentDrawOrderEntry{
                .source = TransparentDrawSource::Particle,
                .unsorted = batch.blend == kb::particles::ParticleRenderBlendMode::Add ||
                    batch.sort == kb::particles::ParticleRenderSortMode::None,
                .depthBucket = batch.transparentDepthBucket,
                .sourceIndex = index,
                .stableTie = (static_cast<std::uint64_t>(batch.emitterRecordIndex) << 32U) | index,
            });
        }
        for (std::uint32_t index = 0U; index < stripBuild.draws.size(); ++index) {
            const ParticleStripDraw& draw = stripBuild.draws[index];
            transparentSubmissionScratch_.push_back(TransparentDrawOrderEntry{
                .source = TransparentDrawSource::ParticleStrip,
                .unsorted = draw.blend == kb::particles::ParticleRenderBlendMode::Add,
                .depthBucket = draw.transparentDepthBucket,
                .sourceIndex = index,
                .stableTie = (static_cast<std::uint64_t>(draw.emitterRecordIndex) << 32U) | index,
            });
        }
        SortTransparentDrawOrder(transparentSubmissionScratch_);
        if (!particleBuild.Succeeded()) {
            ++stats.failedParticleBatchCount;
        }
        stats.droppedParticleCount += particleBuild.droppedParticleCount;
        stats.droppedParticleStripSegmentCount += stripBuild.droppedSegmentCount;
        if (stripBuild.Usable()) {
            stats.particleStripUploadBytes +=
                static_cast<std::uint64_t>(stripBuild.vertices.size_bytes() + stripBuild.indices.size_bytes());
        } else {
            ++stats.failedParticleBatchCount;
            ++stats.failedParticleStripBatchCount;
        }
        if (diagnostics != nullptr) {
            for (const std::uint32_t emitterRecordIndex : particleBuild.unsupportedEmitterRecordIndices) {
                const auto& emitter = particleSnapshot->Emitters()[emitterRecordIndex];
                diagnostics->events.push_back(SceneRenderDiagnosticEvent{
                    .severity = SceneRenderDiagnosticSeverity::Error,
                    .kind = SceneRenderDiagnosticKind::UnsupportedParticleOutput,
                    .materialAssetId = emitter.materialAssetId,
                    .particleEffectAssetId = emitter.effectAssetId,
                    .particleEmitterId = emitter.emitterId,
                    .instanceCount = emitter.particleCount,
                });
            }
        }
        for (const TransparentDrawOrderEntry& entry : transparentSubmissionScratch_) {
            if (entry.source == TransparentDrawSource::Mesh) {
                submitMeshCommands(std::span<const MeshDrawCommand>{
                    &pipelineScratch_.commands[entry.sourceIndex], 1U});
                continue;
            }
            if (entry.source == TransparentDrawSource::Particle && particleBuild.Succeeded()) {
                const ParticleGpuSubmitResult particleResult = particleRenderer->SubmitBatch(
                    viewId, entry.sourceIndex, *particleSnapshot, *camera, resources, resourceMap, sceneDepthTexture);
                stats.submittedParticleCount += particleResult.submittedParticles;
                stats.submittedParticleDrawCallCount += particleResult.drawCalls;
                stats.droppedParticleCount += particleResult.droppedParticles;
                stats.particleInstanceUploadBytes +=
                    static_cast<std::uint64_t>(particleResult.submittedParticles) * sizeof(ParticleGpuInstance);
                if (!particleResult.succeeded) ++stats.failedParticleBatchCount;
            }
            if (entry.source == TransparentDrawSource::ParticleStrip && stripBuild.Usable()) {
                const ParticleStripSubmitResult stripResult = particleRenderer->SubmitStripDraw(viewId, entry.sourceIndex);
                stats.submittedParticleStripSegmentCount += stripResult.submittedIndices / 6U;
                stats.submittedParticleDrawCallCount += stripResult.drawCalls;
                if (!stripResult.succeeded) {
                    ++stats.failedParticleBatchCount;
                    ++stats.failedParticleStripBatchCount;
                }
            }
        }
    } else {
        submitMeshCommands(pipelineScratch_.commands);
    }

    return stats;
}

bool SceneMeshSubmitter::IsInitialized() const noexcept {
    return passResources_.IsInitialized();
}

} // namespace kb::render
