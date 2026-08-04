#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
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
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"

#include <bgfx/bgfx.h>

#include <array>
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
    const kb::scene::SkeletonAsset& skeleton) {
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
    controller.rigConstraints = {
        {
            .name = "Aim",
            .type = kb::scene::AnimatorRigConstraintType::Aim,
            .target = "AimTarget",
            .constrainedBoneId = skeleton.bones.back().id,
        },
    };
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
    Workload workload, kb::render::Renderer& renderer) {
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
                MakeController(skeleton)),
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
                scene.Animators().SetIkTarget(entity, "AimTarget", {
                    .worldPosition = { 1.0F, 1.0F, 1.0F },
                }),
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
    const Clock::time_point animationStartedAt = Clock::now();
    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
    BenchmarkResult result{ .workload = workload };
    result.animationMilliseconds = ElapsedMilliseconds(animationStartedAt);

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
            }
        }
    }
    result.evaluatedPoses -= beforeEvaluationCount;
    result.hierarchySolves -= beforeHierarchyCount;
    Require(result.evaluatedPoses >= workload.rigCount &&
            result.hierarchySolves >= workload.rigCount,
        "SK-12.87 runtime did not execute sampling, blend, constraints and hierarchy solve for every rig");

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
    Require(result.submittedMeshes == workload.rigCount &&
            result.submittedDrawCalls != 0U && !drawStats.HasMissingResources(),
        "SK-12.87 production renderer did not submit every rig draw proxy");

    palettes.Shutdown();
    std::filesystem::remove_all(root, error);
    Require(!error, "SK-12.87 could not remove its dedicated fixture root");
    return result;
}

void PrintResult(const BenchmarkResult& result) {
    std::cout << "SK-12.87 " << result.workload.rigCount << " rigs x "
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
              << " submitted-draw-calls=" << result.submittedDrawCalls << std::endl;
}

} // namespace

int main() {
    try {
        HeadlessSurface surface;
        kb::render::DisplayConfig config{};
        config.allowHeadlessNoop = true;
        config.preferredBgfxRendererType =
            static_cast<std::int32_t>(bgfx::RendererType::Noop);
        kb::render::Renderer renderer;
        Require(renderer.Initialize(surface, &config),
            "SK-12.87 could not initialize the production headless renderer");
        for (const Workload workload : std::array<Workload, 2U>{
                 Workload{ .rigCount = 100U, .boneCount = 100U },
                 Workload{ .rigCount = 1000U, .boneCount = 200U },
             }) {
            PrintResult(RunBenchmark(workload, renderer));
        }
        renderer.Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SK-12.87 FAILED: " << error.what() << '\n';
        return 1;
    }
}
