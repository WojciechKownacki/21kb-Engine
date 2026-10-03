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

[[nodiscard]] bool IsOpaqueNonTerrainBatch(
    const SceneMeshBatch& batch,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap) noexcept {
    if (batch.hasMaterialSlotOverrides) {
        return false;
    }
    const RenderMeshHandle meshHandle = resourceMap.ResolveMesh(batch.meshAssetId);
    const RenderMeshResource* mesh = meshHandle.IsValid() ? resources.FindMesh(meshHandle) : nullptr;
    if (mesh == nullptr || mesh->terrainLayerCount != 0U) return false;
    const auto opaque = [&resources, &resourceMap](std::uint64_t id) {
        // Zero resolves to the built-in opaque fallback in the mesh pipeline.
        if (id == 0U) return true;
        const auto* material = resources.FindMaterial(resourceMap.ResolveMaterial(id));
        return material != nullptr && material->alphaMode != RenderMaterialAlphaMode::Blend;
    };
    if (batch.materialAssetId != 0U) return opaque(batch.materialAssetId);
    return std::ranges::all_of(mesh->materialSlots, [&opaque](const auto& slot) {
        return opaque(slot.defaultMaterialAssetId);
    });
}

} // namespace

bool SceneMeshSubmitter::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    if (!passResources_.Initialize()) {
        Shutdown();
        return false;
    }
    // Compute culling is an acceleration feature, not a renderer prerequisite.
    // WebGL2 and baseline Android GLES3 have a complete CPU visibility/submit
    // path and must not attempt to load or create compute resources.
    static_cast<void>(gpuDrivenCullingPass_.Initialize());
    transparentSubmissionScratch_.reserve(4'096U + kParticleGpuMaxBatches);
    meshBatchSubmissionScratch_.reserve(kb::particles::kParticleRenderSnapshotMaxEmitterRecords);
    particleMeshBatchBuilder_.Warmup(4'096U);

    return true;
}

void SceneMeshSubmitter::Shutdown() {
    instanceBuffers_.Shutdown();
    gpuDrivenFrameResources_.Shutdown();
    gpuDrivenCullingPass_.Shutdown();
    passResources_.Shutdown();
    for (auto& commands : passCommandScratch_) commands.clear();
    for (auto& reuse : passCommandReuse_) reuse.Reset();
    for (auto& reuse : passBatchCommandReuse_) reuse.Reset();
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
    const kb::particles::ParticleRenderSnapshot* particleSnapshot,
    std::span<const ParticleGpuMeshDraw> gpuMeshDraws) const {
    SceneRenderSubmitStats stats{};
    if (!IsInitialized()) {
        return stats;
    }

    auto& passCommands = passCommandScratch_.at(static_cast<std::size_t>(pass));
    pipelineScratch_.commands.swap(passCommands);
    struct RestorePassCommands {
        std::vector<MeshDrawCommand>& active;
        std::vector<MeshDrawCommand>& stored;
        ~RestorePassCommands() { active.swap(stored); }
    } restoreCommands{pipelineScratch_.commands, passCommands};

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
    if (pass == MeshPassType::BaseTransparent) {
        std::erase_if(meshBatchSubmissionScratch_, [&resources, &resourceMap](const SceneMeshBatch& batch) {
            return IsOpaqueNonTerrainBatch(batch, resources, resourceMap);
        });
    }
    SceneRenderSubmitStats lightingStats{};
    PackedSceneLighting lighting = SceneLightingPacker::Build(renderScene, lightingStats, lightingConfig, camera);
    if ((pass == MeshPassType::BaseOpaque || pass == MeshPassType::BaseTransparent) &&
        lightingConfig.lightingPath != SceneRenderLightingPath::Forward &&
        lightingConfig.maxForwardLights != 0U && lightingStats.skippedForwardLightCount != 0U) {
        lighting.lightGrid = passResources_.LightGrid().Prepare(renderScene, lightingConfig,
            camera != nullptr ? camera->cullingMask : 0xFFFFFFFFU, lighting.primaryLightId, lightingStats, diagnostics);
    }
    if (shadowMap != nullptr) {
        SceneLightingPacker::AssignPointShadowSlots(lighting, shadowMap->point);
    }
    const std::array<float, 4> cameraPosition = SceneLightingPacker::CameraPosition(camera);
    if (detailSwitchScene_ != &renderScene) {
        pipelineScratch_.detailSwitchLevels.clear();
        pipelineScratch_.detailSwitchPreviousLevels.clear();
        ++pipelineScratch_.detailSwitchHistoryRevision;
        detailSwitchScene_ = &renderScene;
    }
    auto& reuse = passCommandReuse_.at(static_cast<std::size_t>(pass));
    auto& batchReuse = passBatchCommandReuse_.at(static_cast<std::size_t>(pass));
    const bool reuseAllowed = pass != MeshPassType::BaseTransparent && selectedEntityIds.empty() &&
        renderScene.VisibilityBlockerProxyCount() == 0U &&
        (particleSnapshot == nullptr || particleSnapshot->Emitters().empty());
    const SceneMeshCommandReuseKey reuseKey{
        .sceneRevision = renderScene.MeshContentRevision(),
        .resourceRevision = resources.Revision(),
        .bindingRevision = resourceMap.Revision(),
        .detailSwitchHistoryRevision = pipelineScratch_.detailSwitchHistoryRevision,
        .resources = &resources, .bindings = &resourceMap,
        .camera = SceneMeshCullingCamera(camera),
        .budget = drawBudget, .gpuSupport = gpuDrivenSupport, .terrainLayersOnly = terrainLayersOnly,
    };
    const auto diagnosticCount = diagnostics == nullptr ? 0U : diagnostics->events.size();
    if (reuseAllowed && reuse.Matches(reuseKey)) {
        pipelineScratch_.stats = reuse.Stats();
    } else {
        reuse.Reset();
        const bool retainBatches = reuseAllowed && batchReuse.HasStableCamera(reuseKey);
        if (reuseAllowed) {
            batchReuse.Prepare(reuseKey);
            if (!retainBatches) batchReuse.DiscardContent();
        } else batchReuse.Reset();
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
        .batchCommandCache = retainBatches ? &batchReuse : nullptr,
        }, pipelineScratch_);
        if (reuseAllowed && (diagnostics == nullptr || diagnosticCount == diagnostics->events.size())) {
            auto committedKey = reuseKey;
            committedKey.detailSwitchHistoryRevision = pipelineScratch_.detailSwitchHistoryRevision;
            reuse.Commit(committedKey, pipelineScratch_.commands, pipelineScratch_.stats);
        }
    }
    // Mesh particles simulated on the GPU join this pass's commands for the submission only: the same pipeline
    // resolves their material, state and pass membership; their instances are the emitters' GPU buffers.
    const std::size_t regularCommandCount = pipelineScratch_.commands.size();
    struct DropGpuMeshCommands {
        std::vector<MeshDrawCommand>& commands;
        std::size_t keep;
        ~DropGpuMeshCommands() {
            if (commands.size() > keep) commands.erase(commands.begin() + static_cast<std::ptrdiff_t>(keep), commands.end());
        }
    } dropGpuMeshCommands{ pipelineScratch_.commands, regularCommandCount };
    if (!gpuMeshDraws.empty()) {
        gpuMeshInstanceScratch_.clear();
        gpuMeshInstanceScratch_.reserve(gpuMeshDraws.size());
        gpuMeshBatchScratch_.clear();
        for (std::size_t index = 0U; index < gpuMeshDraws.size(); ++index) {
            const ParticleGpuMeshDraw& draw = gpuMeshDraws[index];
            if (draw.count == 0U || !bgfx::isValid(draw.instances)) continue;
            SceneRenderMeshInstance placeholder{};
            placeholder.entityId = index + 1U; // identifies the draw after the pipeline merged equal meshes
            placeholder.meshAssetId = draw.meshAssetId;
            placeholder.materialAssetId = draw.materialAssetId;
            placeholder.model = { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
            placeholder.castsShadow = draw.castsShadow;
            placeholder.receivesShadow = draw.receivesShadow;
            gpuMeshInstanceScratch_.push_back(placeholder);
        }
        for (const SceneRenderMeshInstance& placeholder : gpuMeshInstanceScratch_) {
            gpuMeshBatchScratch_.push_back(SceneMeshBatch{
                .meshAssetId = placeholder.meshAssetId,
                .materialAssetId = placeholder.materialAssetId,
                .sourceDrawGroupIndex = 0U,
                .instances = std::span<const SceneRenderMeshInstance>{ &placeholder, 1U },
            });
        }
        if (pass == MeshPassType::BaseTransparent) {
            std::erase_if(gpuMeshBatchScratch_, [&resources, &resourceMap](const SceneMeshBatch& batch) {
                return IsOpaqueNonTerrainBatch(batch, resources, resourceMap);
            });
        }
        if (!gpuMeshBatchScratch_.empty()) {
            MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
                .pass = pass,
                .meshBatches = &gpuMeshBatchScratch_,
                .resources = &resources,
                .resourceMap = &resourceMap,
                .diagnostics = diagnostics,
                .maxDrawCommands = drawBudget.maxDrawCommands,
                .maxVisibleInstances = drawBudget.maxVisibleInstances,
                .maxDroppedInstances = drawBudget.maxDroppedInstances,
                .terrainLayersOnly = terrainLayersOnly,
            }, gpuMeshScratch_);
            for (const MeshDrawCommand& command : gpuMeshScratch_.commands) {
                for (const SceneRenderMeshInstance& instance : command.instances) {
                    const std::size_t drawIndex = static_cast<std::size_t>(instance.entityId) - 1U;
                    if (drawIndex >= gpuMeshDraws.size()) continue;
                    MeshDrawCommand gpuCommand = command;
                    gpuCommand.instances.assign(1U, instance);
                    gpuCommand.instanceRevision = 0U;
                    gpuCommand.gpuInstanceBuffer = gpuMeshDraws[drawIndex].instances;
                    gpuCommand.gpuInstanceCount = gpuMeshDraws[drawIndex].count;
                    pipelineScratch_.commands.push_back(std::move(gpuCommand));
                }
            }
        }
    }
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
        .instanceBufferPool = &instanceBuffers_,
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
                .stableTie = index,
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
                stats.submittedVolumetricParticleCount += particleResult.submittedVolumetricParticles;
                stats.volumetricParticleRaymarchStepCount += particleResult.volumetricRaymarchSteps;
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
