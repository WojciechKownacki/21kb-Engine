#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/ecs/SystemSchedulerTrace.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldTelemetry.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/SkeletalMeshRenderResourceBuilder.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/scene/MeshPipeline.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct HeadlessSurface final : kb::render::RenderSurface {
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 64U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 64U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

struct Workload {
    std::size_t rigCount = 0U;
    std::size_t boneCount = 0U;
};

struct BenchmarkResult {
    Workload workload{};
    double animationMilliseconds = 0.0;
    double paletteUploadMilliseconds = 0.0;
    double drawSubmissionMilliseconds = 0.0;
    std::uint64_t evaluatedPoses = 0U;
    std::uint64_t hierarchySolves = 0U;
    std::uint64_t uploadedPaletteBytes = 0U;
    std::uint32_t paletteBatches = 0U;
    std::uint32_t submittedDrawCalls = 0U;
    std::uint32_t submittedMeshes = 0U;
    double updateRate30HzMilliseconds = 0.0;
    double workerDispatchWallMilliseconds = 0.0;
    double workerActiveMilliseconds = 0.0;
    std::size_t workerCount = 0U;
    std::size_t ecsStorageAllocations = 0U;
    std::size_t ecsAllocatedBytes = 0U;
    std::uint64_t ecsQueryCacheMisses = 0U;
    std::uint64_t ecsQueryRecordCacheMisses = 0U;
    std::uint64_t ecsQueryBatches = 0U;
    double ecsAverageBatchSize = 0.0;
    std::uint32_t submittedDrawGroups = 0U;
    std::uint32_t drawCommandCacheHits = 0U;
    std::uint32_t drawCommandCacheMisses = 0U;
    std::uint32_t drawCommandCacheBuilds = 0U;
    std::uint32_t lodSelections = 0U;
    double lodEnabledMilliseconds = 0.0;
    double lodDisabledMilliseconds = 0.0;
    std::size_t parallelPoseEvaluations = 0U;
    std::size_t parallelPoseWorkers = 1U;
    std::size_t updateRateSkippedPoses = 0U;
    double firstPoseFingerprint = 0.0;
    std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> retainedDebugSnapshot;
    bool stressLifecycleCompleted = false;
};

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error{ message };
}

[[nodiscard]] double ElapsedMilliseconds(Clock::time_point startedAt) noexcept {
    return std::chrono::duration<double, std::milli>(Clock::now() - startedAt).count();
}

[[nodiscard]] kb::render::SceneRenderCamera IdentityCamera() noexcept {
    return {
        .view = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
        .projection = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
}

[[nodiscard]] kb::render::RenderSceneSubmitDesc SubmitDesc() noexcept {
    return {
        .target = kb::render::RenderSceneTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .colorTexture = BGFX_INVALID_HANDLE,
            .viewport = kb::render::RenderViewportDesc{
                .id = kb::render::RenderViewportId{ 1U },
                .extent = kb::render::RenderExtent{ 64U, 64U },
                .viewportIndex = 0U,
            },
        },
        .cameraOverride = IdentityCamera(),
    };
}

[[nodiscard]] std::array<float, 16U> TranslationMatrix(float z) noexcept {
    return {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, z, 1.0F,
    };
}

struct LodMeasurement {
    double enabledMilliseconds = 0.0;
    double disabledMilliseconds = 0.0;
    std::uint32_t selections = 0U;
};

[[nodiscard]] LodMeasurement MeasureLodSelection(Workload workload) {
    kb::render::RenderMeshResource mesh{};
    mesh.indexCount = 6U;
    mesh.bounds = kb::render::RenderBoundsSphere{
        .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.05F,
    };
    mesh.sections = {
        kb::render::RenderMeshSection{
            .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds, .lodLevel = 0U,
        },
        kb::render::RenderMeshSection{
            .indexStart = 3U, .indexCount = 3U, .bounds = mesh.bounds, .lodLevel = 1U,
        },
    };
    mesh.lods = {
        kb::render::RenderMeshLodDesc{
            .firstSection = 0U, .sectionCount = 1U, .minScreenCoverage = 0.05F,
        },
        kb::render::RenderMeshLodDesc{
            .firstSection = 1U, .sectionCount = 1U, .minScreenCoverage = 0.0F,
        },
    };

    kb::render::SceneRenderDrawGroup enabledGroup{
        .meshAssetId = 0x534B3132U,
        .materialAssetId = 0x534B3133U,
    };
    enabledGroup.instances.reserve(workload.rigCount);
    for (std::size_t index = 0U; index < workload.rigCount; ++index) {
        enabledGroup.instances.push_back(kb::render::SceneRenderMeshInstance{
            .entityId = index + 1U,
            .meshAssetId = enabledGroup.meshAssetId,
            .materialAssetId = enabledGroup.materialAssetId,
            .model = TranslationMatrix(index % 2U == 0U ? 0.1F : 0.9F),
            .lodEnabled = true,
        });
    }
    kb::render::SceneRenderDrawGroup disabledGroup = enabledGroup;
    for (kb::render::SceneRenderMeshInstance& instance : disabledGroup.instances) {
        instance.lodEnabled = false;
    }
    const std::vector<kb::render::SceneRenderDrawGroup> enabledGroups{ enabledGroup };
    const std::vector<kb::render::SceneRenderDrawGroup> disabledGroups{ disabledGroup };
    const kb::render::SceneRenderCamera camera = IdentityCamera();
    const auto measure = [&](const std::vector<kb::render::SceneRenderDrawGroup>& groups) {
        kb::render::MeshPipelineBuildResult result;
        const kb::render::MeshPipelineBuildDesc desc{
            .pass = kb::render::MeshPassType::BaseOpaque,
            .drawGroups = &groups,
            .resolvedMeshResource = &mesh,
            .camera = &camera,
            .resourceValidation = kb::render::MeshPipelineResourceValidation::Skip,
        };
        kb::render::MeshPipelineProcessor::BuildInto(desc, result);
        constexpr std::uint32_t kSamples = 5U;
        const Clock::time_point startedAt = Clock::now();
        for (std::uint32_t sample = 0U; sample < kSamples; ++sample) {
            kb::render::MeshPipelineProcessor::BuildInto(desc, result);
        }
        return std::pair{
            ElapsedMilliseconds(startedAt) / static_cast<double>(kSamples), result.stats,
        };
    };

    const auto [enabledMilliseconds, enabledStats] = measure(enabledGroups);
    const auto [disabledMilliseconds, disabledStats] = measure(disabledGroups);
    Require(enabledStats.lodSelectionCount == workload.rigCount * mesh.sections.size() &&
            disabledStats.lodSelectionCount == workload.rigCount * mesh.sections.size(),
        "SK-12.88 production LOD pipeline did not evaluate every rig and section");
    return {
        .enabledMilliseconds = enabledMilliseconds,
        .disabledMilliseconds = disabledMilliseconds,
        .selections = enabledStats.lodSelectionCount,
    };
}

void WriteTriangleObj(const std::filesystem::path& path) {
    std::ofstream output{ path, std::ios::trunc };
    Require(output.good(), "SK-12.87 could not create draw-submission mesh fixture");
    output
        << "v -0.05 -0.05 0.0\n"
        << "v 0.05 -0.05 0.0\n"
        << "v 0.0 0.05 0.0\n"
        << "vt 0 0\n"
        << "vt 1 0\n"
        << "vt 0.5 1\n"
        << "vn 0 0 1\n"
        << "f 1/1/1 2/2/1 3/3/1\n";
}

[[nodiscard]] kb::scene::SkeletonAsset MakeSkeleton(std::size_t boneCount) {
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones.reserve(boneCount);
    for (std::size_t index = 0U; index < boneCount; ++index) {
        skeleton.bones.push_back({
            .id = static_cast<kb::scene::SkeletonBoneId>(index + 1U),
            .parentIndex = index == 0U ? -1 : static_cast<std::int32_t>(index - 1U),
            .name = "Bone" + std::to_string(index),
            .referencePose = { .position = { 0.0F, index == 0U ? 0.0F : 0.01F, 0.0F } },
            .inverseBind = {},
        });
    }
    return skeleton;
}

[[nodiscard]] kb::scene::AnimationClip MakeClip(
    const kb::scene::SkeletonAsset& skeleton, std::uint64_t skeletonAssetId,
    std::uint64_t skeletonSignature, float amplitude) {
    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 1.0F;
    clip.looping = true;
    clip.targetSkeletonAssetId = skeletonAssetId;
    clip.targetSkeletonCompatibilitySignature = skeletonSignature;
    clip.skeletalTracks.reserve(skeleton.bones.size());
    for (const kb::scene::SkeletonBone& bone : skeleton.bones) {
        clip.skeletalTracks.push_back({
            .boneId = bone.id,
            .bindingMask = ~std::uint64_t{ 0U },
            .keyframes = {
                { .timeSeconds = 0.0F, .transform = {} },
                { .timeSeconds = 1.0F, .transform = {
                    .position = { amplitude, 0.0F, 0.0F },
                } },
            },
        });
    }
    return clip;
}

[[nodiscard]] kb::scene::AnimatorController MakeController(
    const kb::scene::SkeletonAsset& skeleton, bool includeConstraint = true) {
    kb::scene::AnimatorController controller{};
    controller.parameters = {
        { .name = "Blend", .type = kb::scene::AnimatorParameterType::Float,
          .floatDefault = 0.5F },
    };
    controller.layers = {
        {
            .name = "Base",
            .defaultState = "Blend",
            .weight = 1.0F,
            .mask = ~std::uint64_t{ 0U },
            .states = {
                {
                    .id = 1U,
                    .name = "Blend",
                    .blendParameter = "Blend",
                    .blendChildren = {
                        { .threshold = 0.0F,
                          .clipReference = "/Game/Animation/BenchA.kbanim" },
                        { .threshold = 1.0F,
                          .clipReference = "/Game/Animation/BenchB.kbanim" },
                    },
                },
            },
        },
    };
    if (includeConstraint) {
        controller.rigConstraints = {
            {
                .name = "Aim",
                .type = kb::scene::AnimatorRigConstraintType::Aim,
                .target = "AimTarget",
                .constrainedBoneId = skeleton.bones.back().id,
            },
        };
    }
    return controller;
}

[[nodiscard]] kb::render::RenderSkinningMatrix ToRenderMatrix(
    const kb::math::Mat4& matrix) noexcept {
    return {
        matrix.columns[0].x, matrix.columns[0].y,
        matrix.columns[0].z, matrix.columns[0].w,
        matrix.columns[1].x, matrix.columns[1].y,
        matrix.columns[1].z, matrix.columns[1].w,
        matrix.columns[2].x, matrix.columns[2].y,
        matrix.columns[2].z, matrix.columns[2].w,
        matrix.columns[3].x, matrix.columns[3].y,
        matrix.columns[3].z, matrix.columns[3].w,
    };
}

[[nodiscard]] BenchmarkResult RunBenchmark(
    Workload workload, kb::render::Renderer& renderer,
    bool includeConstraint = true, float poseUpdateRateHz = 0.0F,
    bool stressLifecycle = false) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("21kb-sk12-87-" + std::to_string(workload.rigCount) + "x" +
         std::to_string(workload.boneCount));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    Require(!error, "SK-12.87 could not clear its dedicated fixture root");

    const std::filesystem::path skeletonPath = root / "Assets" / "Skeletal" /
        "Bench.kbskeleton";
    const kb::scene::SkeletonAsset skeleton = MakeSkeleton(workload.boneCount);
    const std::uint64_t signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
    Require(signature != 0U && kb::scene::SkeletonAssetIO::Save(skeletonPath, skeleton),
        "SK-12.87 could not save the production skeleton fixture");

    const std::filesystem::path meshPath = root / "Assets" / "Meshes" /
        "RigTriangle.obj";
    std::filesystem::create_directories(meshPath.parent_path(), error);
    Require(!error, "SK-12.87 could not create the mesh fixture folder");
    WriteTriangleObj(meshPath);

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root),
        "SK-12.87 could not mount the production fixture project");
    Require(scene.Assets().Manager().RegisterLoader(
                std::make_unique<kb::render::RenderMeshAssetLoader>()),
        "SK-12.87 could not register the production render-mesh loader");
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* skeletonMetadata =
        scene.Assets().Manager().Registry().FindByPath(
            "/Game/Skeletal/Bench.kbskeleton");
    Require(skeletonMetadata != nullptr,
        "SK-12.87 could not discover the production skeleton asset");
    const kb::assets::AssetId skeletonId = skeletonMetadata->id;

    Require(kb::scene::AnimationAssetIO::SaveClip(
                root / "Assets" / "Animation" / "BenchA.kbanim",
                MakeClip(skeleton, skeletonId.value, signature, 0.01F)) &&
            kb::scene::AnimationAssetIO::SaveClip(
                root / "Assets" / "Animation" / "BenchB.kbanim",
                MakeClip(skeleton, skeletonId.value, signature, 0.02F)) &&
            kb::scene::AnimationAssetIO::SaveController(
                root / "Assets" / "Animation" / "Bench.kbanimcontroller",
                MakeController(skeleton, includeConstraint)),
        "SK-12.87 could not save production animation assets");
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetMetadata* controllerMetadata =
        scene.Assets().Manager().Registry().FindByPath(
            "/Game/Animation/Bench.kbanimcontroller");
    const kb::assets::AssetMetadata* meshMetadata =
        scene.Assets().Manager().Registry().FindByPath("/Game/Meshes/RigTriangle.obj");
    Require(controllerMetadata != nullptr && meshMetadata != nullptr,
        "SK-12.87 could not discover controller or render-mesh assets");

    std::vector<kb::scene::SceneEntity> rigs;
    rigs.reserve(workload.rigCount);
    for (std::size_t index = 0U; index < workload.rigCount; ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity({
            .name = "Rig" + std::to_string(index),
        });
        Require(scene.Components().SkeletonBindings().Set(entity, {
                    .skeletonAssetId = skeletonId.value,
                    .skeletonCompatibilitySignature = signature,
                    .enabled = true,
                }), "SK-12.87 could not attach the runtime skeleton binding");
        scene.Components().Animators().Set(entity, {
            .controllerAssetId = controllerMetadata->id.value,
            .speed = 1.0F,
            .poseUpdateRateHz = poseUpdateRateHz,
            .enabled = true,
        });
        scene.Components().MeshRenderers().Set(entity, {
            .meshAssetId = meshMetadata->id.value,
            .materialAssetId = 0x534B3132U,
        });
        Require(scene.Components().Animators().Has(entity) &&
                scene.Components().MeshRenderers().Has(entity),
            "SK-12.87 could not attach runtime rig render components");
        rigs.push_back(entity);
    }

    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    for (const kb::scene::SceneEntity entity : rigs) {
        Require(scene.Animators().SetFloat(entity, "Blend", 0.5F) &&
                (!includeConstraint || scene.Animators().SetIkTarget(entity, "AimTarget", {
                    .worldPosition = { 1.0F, 1.0F, 1.0F },
                })),
            "SK-12.87 could not configure live blend or constraint target");
    }
    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));

    std::uint64_t beforeEvaluationCount = 0U;
    std::uint64_t beforeHierarchyCount = 0U;
    for (const kb::scene::SceneEntity entity : rigs) {
        const auto instance = scene.Animators().InstanceSkeleton(entity);
        Require(instance.has_value(), "SK-12.87 runtime did not attach a skeletal animator");
        beforeEvaluationCount += instance->evaluationCount;
        beforeHierarchyCount += instance->hierarchySolveCount;
    }
    kb::ecs::World& ecsWorld = scene.Runtime().EcsWorld();
    ecsWorld.ResetTelemetryFrameCounters();
    const kb::ecs::WorldTelemetrySnapshot telemetryBefore = ecsWorld.TelemetrySnapshot();
    scene.Runtime().SetEcsProfilerEnabled(true);
    const Clock::time_point animationStartedAt = Clock::now();
    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
    BenchmarkResult result{ .workload = workload };
    result.animationMilliseconds = ElapsedMilliseconds(animationStartedAt);
    const kb::ecs::WorldTelemetrySnapshot telemetryAfter = ecsWorld.TelemetrySnapshot();
    const kb::ecs::SystemSchedulerTrace profilerTrace = scene.Runtime().LastEcsProfilerTrace();
    scene.Runtime().SetEcsProfilerEnabled(false);
    result.workerDispatchWallMilliseconds = static_cast<double>(
        profilerTrace.frameCounters.lastWorkerDispatchWallNanoseconds) / 1'000'000.0;
    result.workerActiveMilliseconds = static_cast<double>(
        profilerTrace.frameCounters.lastWorkerActiveNanoseconds) / 1'000'000.0;
    result.workerCount = profilerTrace.frameCounters.lastWorkerDispatchActiveWorkerCount;
    result.ecsStorageAllocations = telemetryAfter.storageSystemAllocationsSinceReset;
    result.ecsAllocatedBytes = telemetryAfter.allocatedBytes;
    result.ecsQueryCacheMisses = telemetryAfter.queryCacheMisses >= telemetryBefore.queryCacheMisses
        ? telemetryAfter.queryCacheMisses - telemetryBefore.queryCacheMisses : 0U;
    result.ecsQueryRecordCacheMisses = telemetryAfter.queryRecordCacheMisses;
    result.ecsQueryBatches = telemetryAfter.queryBatches;
    result.ecsAverageBatchSize = telemetryAfter.queryAverageEffectiveBatchSize;
    const kb::scene::SceneRuntimeHotPathReport hotPath = scene.Runtime().HotPathReport();
    result.parallelPoseEvaluations = hotPath.animatorParallelEvaluationCount;
    result.parallelPoseWorkers = hotPath.animatorParallelWorkerCount;
    result.updateRateSkippedPoses = hotPath.animatorUpdateRateSkippedPoseCount;

    std::vector<kb::render::RenderSkinningMatrix> palette;
    for (const kb::scene::SceneEntity entity : rigs) {
        const auto instance = scene.Animators().InstanceSkeleton(entity);
        Require(instance.has_value() && instance->currentSkinMatrices.size() == workload.boneCount,
            "SK-12.87 runtime pose did not retain every benchmark bone");
        result.evaluatedPoses += instance->evaluationCount;
        result.hierarchySolves += instance->hierarchySolveCount;
        if (palette.empty()) {
            palette.reserve(instance->currentSkinMatrices.size());
            for (const kb::math::Mat4& matrix : instance->currentSkinMatrices) {
                palette.push_back(ToRenderMatrix(matrix));
                result.firstPoseFingerprint +=
                    static_cast<double>(matrix.columns[0].x) +
                    static_cast<double>(matrix.columns[1].y) +
                    static_cast<double>(matrix.columns[2].z) +
                    static_cast<double>(matrix.columns[3].x) +
                    static_cast<double>(matrix.columns[3].y) +
                    static_cast<double>(matrix.columns[3].z);
            }
        }
    }
    result.evaluatedPoses -= beforeEvaluationCount;
    result.hierarchySolves -= beforeHierarchyCount;
    if (poseUpdateRateHz == 0.0F) {
        Require(result.evaluatedPoses >= workload.rigCount &&
                result.hierarchySolves >= workload.rigCount,
            "SK-12.87 runtime did not execute sampling, blend, constraints and hierarchy solve for every rig");
    }

    const Clock::time_point updateRate30HzStartedAt = Clock::now();
    static_cast<void>(scene.Runtime().Update(1.0F / 30.0F));
    result.updateRate30HzMilliseconds = ElapsedMilliseconds(updateRate30HzStartedAt);

    constexpr std::uint32_t kPalettePageCapacity = UINT16_MAX;
    kb::render::RenderSkinningPaletteAllocator palettes{
        kb::render::RenderSkinningPaletteAllocatorDesc{
            .matrixCapacityPerFrame = kPalettePageCapacity,
        } };
    const Clock::time_point uploadStartedAt = Clock::now();
    std::size_t uploadedRigs = 0U;
    std::uint64_t frame = 1U;
    while (uploadedRigs < rigs.size()) {
        Require(palettes.BeginFrame(frame, frame),
            "SK-12.87 could not begin a skinning palette upload page");
        const std::size_t rigsPerPage = kPalettePageCapacity / palette.size();
        const std::size_t pageEnd = std::min(rigs.size(), uploadedRigs + rigsPerPage);
        for (; uploadedRigs < pageEnd; ++uploadedRigs) {
            const kb::render::RenderSkinningPaletteHandle handle =
                palettes.Allocate(static_cast<std::uint32_t>(palette.size()));
            Require(handle.IsValid() && palettes.Upload(handle, palette),
                "SK-12.87 production palette upload failed");
        }
        result.uploadedPaletteBytes += palettes.Stats().uploadedBytes;
        ++result.paletteBatches;
        frame += 2U;
        bgfx::frame();
    }
    result.paletteUploadMilliseconds = ElapsedMilliseconds(uploadStartedAt);
    Require(result.uploadedPaletteBytes ==
            workload.rigCount * workload.boneCount * sizeof(kb::render::RenderSkinningMatrix),
        "SK-12.87 palette upload did not include every rig matrix");

    const kb::render::RenderSceneSubmitDesc desc = SubmitDesc();
    Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
        "SK-12.87 could not warm the production draw-submission path");
    renderer.EndFrame();
    Require(renderer.BeginFrame(), "SK-12.87 could not begin the measured draw frame");
    const Clock::time_point drawStartedAt = Clock::now();
    Require(renderer.SubmitScene(scene, desc),
        "SK-12.87 production draw submission failed");
    result.drawSubmissionMilliseconds = ElapsedMilliseconds(drawStartedAt);
    const kb::render::SceneRenderSubmitStats drawStats = renderer.LastSceneSubmitStats();
    renderer.EndFrame();
    result.submittedMeshes = drawStats.submittedMeshCount;
    result.submittedDrawCalls = drawStats.submittedDrawCallCount;
    result.submittedDrawGroups = drawStats.submittedDrawGroupCount;
    result.drawCommandCacheHits = drawStats.meshDrawCommandCacheHitCount;
    result.drawCommandCacheMisses = drawStats.meshDrawCommandCacheMissCount;
    result.drawCommandCacheBuilds = drawStats.meshDrawCommandCacheBuildCount;
    Require(result.submittedMeshes == workload.rigCount &&
            result.submittedDrawCalls != 0U && !drawStats.HasMissingResources(),
        "SK-12.87 production renderer did not submit every rig draw proxy");

    if (stressLifecycle) {
        scene.Animators().WaitForDebugSnapshot();
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> beforeReload =
            scene.Animators().DebugSnapshot();
        kb::scene::AnimatorController reimportedController =
            MakeController(skeleton, includeConstraint);
        reimportedController.parameters.front().floatDefault = 0.75F;
        const bool controllerCached = [&scene, controllerMetadata] {
            return scene.Assets().Manager()
                .Load<kb::scene::AnimatorController>(controllerMetadata->id)
                .IsLoaded();
        }();
        Require(beforeReload != nullptr && controllerCached &&
                kb::scene::AnimationAssetIO::SaveController(
                    root / "Assets" / "Animation" / "Bench.kbanimcontroller",
                    reimportedController) &&
                scene.Assets().Manager().Unload(controllerMetadata->id),
            "SK-12.90 could not reimport and evict the live animator controller");
        static_cast<void>(scene.Runtime().Update(0.0F));
        scene.Animators().WaitForDebugSnapshot();
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> afterReload =
            scene.Animators().DebugSnapshot();
        Require(afterReload != nullptr && afterReload->revision > beforeReload->revision &&
                afterReload->instances.size() == rigs.size() &&
                afterReload->instances.front().runtimeBindingGeneration !=
                    beforeReload->instances.front().runtimeBindingGeneration,
            "SK-12.90 animation reload did not publish a fresh production debug snapshot");

        const kb::scene::SceneEntity destroyedEntity = rigs.front();
        scene.Entities().QueueDeferredDestroy(destroyedEntity);
        Require(scene.Entities().DrainDeferredDestroys() == 1U &&
                !scene.Entities().IsAlive(destroyedEntity),
            "SK-12.90 deferred entity destruction did not complete while debugging");
        result.retainedDebugSnapshot = afterReload;
        Require(scene.Assets().Manager().Unload(meshMetadata->id),
            "SK-12.90 could not evict the live render mesh asset");
        Require(!scene.Assets().Manager().IsLoaded(meshMetadata->id),
            "SK-12.90 render mesh eviction did not remove the cache entry");
        result.stressLifecycleCompleted = true;
    } else {
        scene.Animators().WaitForDebugSnapshot();
        result.retainedDebugSnapshot = scene.Animators().DebugSnapshot();
    }

    const LodMeasurement lod = MeasureLodSelection(workload);
    result.lodSelections = lod.selections;
    result.lodEnabledMilliseconds = lod.enabledMilliseconds;
    result.lodDisabledMilliseconds = lod.disabledMilliseconds;

    palettes.Shutdown();
    std::filesystem::remove_all(root, error);
    Require(!error, "SK-12.87 could not remove its dedicated fixture root");
    return result;
}

void PrintResult(const BenchmarkResult& result) {
    std::cout << "SK-12.87/88 " << result.workload.rigCount << " rigs x "
              << result.workload.boneCount << " bones: "
              << "sampling+blend+constraints+hierarchy="
              << std::fixed << std::setprecision(3) << result.animationMilliseconds << "ms"
              << " pose-evaluations=" << result.evaluatedPoses
              << " hierarchy-solves=" << result.hierarchySolves
              << " palette-upload=" << result.paletteUploadMilliseconds << "ms"
              << " palette-bytes=" << result.uploadedPaletteBytes
              << " palette-batches=" << result.paletteBatches
              << " draw-submission=" << result.drawSubmissionMilliseconds << "ms"
              << " submitted-meshes=" << result.submittedMeshes
              << " submitted-draw-groups=" << result.submittedDrawGroups
              << " submitted-draw-calls=" << result.submittedDrawCalls
              << " draw-command-cache-hits=" << result.drawCommandCacheHits
              << " draw-command-cache-misses=" << result.drawCommandCacheMisses
              << " draw-command-cache-builds=" << result.drawCommandCacheBuilds
              << " main-thread-update-60hz=" << result.animationMilliseconds << "ms"
              << " worker-dispatch-wall=" << result.workerDispatchWallMilliseconds << "ms"
              << " worker-active=" << result.workerActiveMilliseconds << "ms"
              << " active-workers=" << result.workerCount
              << " update-rate-30hz=" << result.updateRate30HzMilliseconds << "ms"
              << " ecs-storage-allocations=" << result.ecsStorageAllocations
              << " ecs-allocated-bytes=" << result.ecsAllocatedBytes
              << " ecs-query-cache-misses=" << result.ecsQueryCacheMisses
              << " ecs-query-record-cache-misses=" << result.ecsQueryRecordCacheMisses
              << " ecs-query-batches=" << result.ecsQueryBatches
              << " ecs-average-batch-size=" << result.ecsAverageBatchSize
              << " lod-selections=" << result.lodSelections
              << " lod-enabled=" << result.lodEnabledMilliseconds << "ms"
              << " lod-disabled=" << result.lodDisabledMilliseconds << "ms"
              << " parallel-pose-evaluations=" << result.parallelPoseEvaluations
              << " parallel-pose-workers=" << result.parallelPoseWorkers
              << " update-rate-skipped-poses=" << result.updateRateSkippedPoses
              << std::endl;
}

void VerifySk1289(kb::render::Renderer& renderer) {
    const BenchmarkResult serial = RunBenchmark(
        Workload{ .rigCount = 31U, .boneCount = 10U }, renderer, false);
    const BenchmarkResult parallel = RunBenchmark(
        Workload{ .rigCount = 32U, .boneCount = 10U }, renderer, false);
    Require(parallel.parallelPoseEvaluations == parallel.workload.rigCount &&
            parallel.parallelPoseWorkers > 1U &&
            parallel.firstPoseFingerprint == serial.firstPoseFingerprint,
        "SK-12.89 parallel skeletal evaluation did not preserve the serial pose result");
    const BenchmarkResult rateLimited = RunBenchmark(
        Workload{ .rigCount = 32U, .boneCount = 10U }, renderer, false, 20.0F);
    Require(rateLimited.evaluatedPoses == 0U &&
            rateLimited.updateRateSkippedPoses == rateLimited.workload.rigCount,
        "SK-12.89 pose update-rate optimization did not deterministically skip only the scheduled pose sample");
}

void VerifySk1290(
    kb::render::Renderer& renderer, HeadlessSurface& surface,
    kb::render::DisplayConfig& config) {
    BenchmarkResult stressed = RunBenchmark(
        Workload{ .rigCount = 8U, .boneCount = 10U }, renderer, false, 0.0F, true);
    Require(stressed.stressLifecycleCompleted && stressed.retainedDebugSnapshot != nullptr &&
            stressed.retainedDebugSnapshot->instances.size() == stressed.workload.rigCount,
        "SK-12.90 scene teardown precondition did not retain the immutable debug snapshot");
    renderer.Shutdown();
    Require(renderer.Initialize(surface, &config),
        "SK-12.90 could not reset the production headless renderer");
    Require(stressed.retainedDebugSnapshot->instances.size() == stressed.workload.rigCount,
        "SK-12.90 scene unload invalidated the immutable debug snapshot");
    const BenchmarkResult afterRendererReset = RunBenchmark(
        Workload{ .rigCount = 8U, .boneCount = 10U }, renderer, false);
    Require(afterRendererReset.submittedMeshes == afterRendererReset.workload.rigCount,
        "SK-12.90 runtime scene did not submit after renderer reset");
}

void ProbeSkeletalMeshAsset(
    kb::render::Renderer& renderer,
    const std::filesystem::path& meshPath,
    const std::filesystem::path& skeletonPath) {
    std::string meshError;
    std::string skeletonError;
    const std::optional<kb::scene::SkeletalMeshAsset> mesh =
        kb::scene::SkeletalMeshAssetIO::Load(meshPath, &meshError);
    const std::optional<kb::scene::SkeletonAsset> skeleton =
        kb::scene::SkeletonAssetIO::Load(skeletonPath, &skeletonError);
    Require(mesh.has_value(), meshError.empty() ? "Skeletal Mesh probe could not load the mesh" : meshError.c_str());
    Require(skeleton.has_value(), skeletonError.empty() ? "Skeletal Mesh probe could not load the skeleton" : skeletonError.c_str());
    Require(mesh->skeletonAssetId != 0U &&
            mesh->skeletonCompatibilitySignature == kb::scene::SkeletonCompatibilitySignature(*skeleton),
        "Skeletal Mesh probe found an incompatible mesh/skeleton pair");

    constexpr kb::assets::AssetId meshId{ 0x534B454C4554414CULL };
    const kb::assets::AssetId skeletonId{ mesh->skeletonAssetId };
    kb::scene::Scene scene{ kb::scene::SceneMode::Runtime };
    kb::assets::AssetManager& assets = scene.Assets().Manager();
    Require(assets.RegisterAsset({
                .id = skeletonId,
                .type = kb::scene::kSkeletonAssetType,
                .name = "Skeletal probe skeleton",
                .virtualPath = "/__Probe/Skeleton.kbskeleton",
                .runtimeLoadable = true,
            }) &&
            assets.RegisterAsset({
                .id = meshId,
                .type = kb::scene::kSkeletalMeshAssetType,
                .name = "Skeletal probe mesh",
                .virtualPath = "/__Probe/Mesh.kbskeletalmesh",
                .runtimeLoadable = true,
            }) &&
            assets.PublishRuntimeAsset(skeletonId, std::make_shared<kb::scene::SkeletonAsset>(*skeleton)) &&
            assets.PublishRuntimeAsset(meshId, std::make_shared<kb::scene::SkeletalMeshAsset>(*mesh)),
        "Skeletal Mesh probe could not publish its immutable assets");

    kb::scene::SceneObjectDesc object{ .name = "Skeletal probe" };
    object.transform.localRotation = kb::scene::Quat{ 0.0F, 0.0F, 1.0F, 0.0F };
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(object);
    scene.Components().MeshRenderers().Set(entity, { .meshAssetId = meshId.value });
    const bool geometrySet = scene.Components().DeformedGeometries().Set(entity, {
        .skeletalMeshAssetId = meshId.value,
        .fixedBounds = true,
        .enabled = true,
    });
    const bool bindingSet = scene.Components().SkeletonBindings().Set(entity, {
        .skeletonAssetId = skeletonId.value,
        .skeletonCompatibilitySignature = mesh->skeletonCompatibilitySignature,
        .enabled = true,
    });
    Require(geometrySet && bindingSet && scene.Components().MeshRenderers().Has(entity) &&
            scene.Components().DeformedGeometries().Has(entity) &&
            scene.Components().SkeletonBindings().Has(entity),
        "Skeletal Mesh probe could not create the renderable entity");
    static_cast<void>(scene.Runtime().Update(0.0F));
    const std::optional<kb::scene::AnimatorInstanceSkeletonView> pose = scene.Animators().InstanceSkeleton(entity);
    Require(pose.has_value() && pose->boneIds.size() == skeleton->bones.size(),
        "Skeletal Mesh probe did not create a complete reference pose");
    std::vector<kb::scene::SkeletonBoneId> paletteBones;
    std::vector<kb::math::Mat4> paletteMatrices;
    const bool currentPaletteBuilt = kb::scene::BuildSkeletalMeshSkinningPalette(
        *mesh, pose->boneIds, pose->currentSkinMatrices, paletteBones, paletteMatrices);
    const bool previousPaletteBuilt = kb::scene::BuildSkeletalMeshSkinningPalette(
        *mesh, pose->boneIds, pose->previousSkinMatrices, paletteBones, paletteMatrices);
    const std::optional<kb::render::SkeletalMeshRenderResourceData> builtMesh =
        kb::render::SkeletalMeshRenderResourceBuilder::BuildValidated(*mesh);

    kb::render::RenderSceneSubmitDesc desc = SubmitDesc();
    Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
        "Skeletal Mesh probe could not submit its warm-up frame");
    renderer.EndFrame();
    desc.synchronizeScene = false;
    kb::render::SceneRenderSubmitStats stats{};
    for (std::uint32_t frame = 0U; frame < 4U; ++frame) {
        Require(renderer.BeginFrame() && renderer.SubmitScene(scene, desc),
            "Skeletal Mesh probe could not submit an unchanged camera-only frame");
        stats = renderer.LastSceneSubmitStats();
        Require(stats.submittedDrawCallCount != 0U,
            "Skeletal Mesh probe lost its frame-local skinning palette after the warm-up frame");
        renderer.EndFrame();
    }
    const kb::render::Renderer::RuntimeSceneResourceStats resourceStats =
        renderer.RuntimeResourceStats();
    const kb::assets::AssetMetadata* finalMeshMetadata = assets.Registry().Find(meshId);
    const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> finalMeshAsset =
        assets.AcquireLoaded<kb::scene::SkeletalMeshAsset>(meshId);
    std::cout << "skeletal-asset-probe bones=" << skeleton->bones.size()
              << " vertices=" << mesh->lods.front().vertices.size()
              << " indices=" << mesh->lods.front().indices.size()
              << " poseAsset=" << pose->skeletonAssetId
              << " meshAsset=" << mesh->skeletonAssetId
              << " poseCompatibility=" << pose->compatibilitySignature
              << " meshCompatibility=" << mesh->skeletonCompatibilitySignature
              << " currentMatrices=" << pose->currentSkinMatrices.size()
              << " previousMatrices=" << pose->previousSkinMatrices.size()
              << " currentPalette=" << currentPaletteBuilt
              << " previousPalette=" << previousPaletteBuilt
              << " meshBuilt=" << builtMesh.has_value()
              << " builtVertices=" << (builtMesh ? builtMesh->desc.vertexCount : 0U)
              << " builtIndices=" << (builtMesh ? builtMesh->desc.indexCount : 0U)
              << " finalMetadata=" << (finalMeshMetadata != nullptr)
              << " finalType=" << (finalMeshMetadata != nullptr ? finalMeshMetadata->type : std::string{})
              << " finalLoaded=" << finalMeshAsset.IsLoaded()
              << " cachedMeshes=" << resourceStats.cachedMeshCount
              << " meshSlots=" << resourceStats.meshResourceSlotCapacity
              << " meshBindings=" << resourceStats.meshBindingCapacity
              << " renderProxies=" << resourceStats.renderSceneMeshProxyCount
              << " visibleMeshes=" << stats.visibleMeshCount
              << " submittedMeshes=" << stats.submittedMeshCount
              << " drawCalls=" << stats.submittedDrawCallCount
              << " culled=" << stats.culledInstanceCount
              << " dropped=" << stats.droppedInstanceCount
              << " missingBinding=" << stats.missingMeshBindingCount
              << " missingMesh=" << stats.missingMeshResourceCount
              << " unsupportedVertex=" << stats.unsupportedMeshVertexFormatCount
              << " missingMaterialBinding=" << stats.missingMaterialBindingCount
              << " missingMaterial=" << stats.missingMaterialResourceCount
              << " diagnostics=" << renderer.LastSceneDiagnostics().events.size() << '\n';
    const std::uint32_t expectedSections =
        static_cast<std::uint32_t>(mesh->lods.front().sections.size());
    Require(stats.visibleMeshCount == expectedSections &&
            stats.submittedMeshCount == expectedSections &&
            stats.submittedDrawCallCount != 0U &&
            stats.missingMeshBindingCount == 0U &&
            stats.missingMeshResourceCount == 0U &&
            stats.unsupportedMeshVertexFormatCount == 0U,
        "Skeletal Mesh probe did not reach a visible GPU draw");
}

} // namespace

int main(int argc, char** argv) {
    try {
        HeadlessSurface surface;
        kb::render::DisplayConfig config{};
        config.allowHeadlessNoop = true;
        config.preferredBgfxRendererType =
            static_cast<std::int32_t>(bgfx::RendererType::Noop);
        kb::render::Renderer renderer;
        Require(renderer.Initialize(surface, &config),
            "SK-12.87 could not initialize the production headless renderer");
        if (argc == 4 && std::string_view{ argv[1] } == "--probe-skeletal-asset") {
            ProbeSkeletalMeshAsset(renderer, argv[2], argv[3]);
            renderer.Shutdown();
            return 0;
        }
        const bool sk1289Only = argc == 2 && std::string_view{ argv[1] } == "--sk12-89-only";
        const bool sk1290Only = argc == 2 && std::string_view{ argv[1] } == "--sk12-90-only";
        if (!sk1289Only && !sk1290Only) {
            for (const Workload workload : std::array<Workload, 2U>{
                     Workload{ .rigCount = 100U, .boneCount = 100U },
                     Workload{ .rigCount = 1000U, .boneCount = 200U },
                 }) {
                PrintResult(RunBenchmark(workload, renderer));
            }
        }
        if (!sk1290Only) VerifySk1289(renderer);
        if (!sk1289Only) VerifySk1290(renderer, surface, config);
        renderer.Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SK-12.87 FAILED: " << error.what() << '\n';
        return 1;
    }
}
