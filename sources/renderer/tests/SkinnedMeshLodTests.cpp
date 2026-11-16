#include "RendererTestSupport.hpp"
#include "SkinnedMeshLodFixture.hpp"

#include "engine/scene/SkeletalMeshAsset.hpp"
#include "kb/render/bake/MeshBaker.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace kb::render::tests {
namespace {

void RunSkeletalMeshLodChainTest() {
    // About twenty thousand triangles on a two-bone skeleton get three coarser levels at half, a quarter and an
    // eighth of the triangles, every vertex keeping its own bone indices and weights.
    kb::scene::SkeletalMeshAsset mesh = MakeSkinnedSphereAsset(100U, 101U, 7U, 11U);
    const std::size_t finest = mesh.lods[0].indices.size() / 3U;
    Require(finest > 19000U && finest < 21000U, "The skinned sphere fixture is not about twenty thousand triangles");
    const kb::scene::SkeletalMeshAsset original = mesh;
    kb::assets::AssetMeshLodSettings settings{};
    settings.levelCount = 4U;
    Require(bake::BuildSkeletalMeshLods(mesh, settings) == bake::MeshBakeStatus::Success && mesh.lods.size() == 4U,
        "A skinned sphere did not get its three coarser levels");
    Require(kb::scene::ValidateSkeletalMeshAsset(mesh).valid, "The generated levels are not a valid skeletal mesh");
    Require(mesh.lods[0].vertices.size() == original.lods[0].vertices.size() && mesh.lods[0].indices == original.lods[0].indices,
        "Generating levels changed the finest level");
    std::string shares;
    std::string coverages;
    for (std::size_t level = 1U; level < mesh.lods.size(); ++level) {
        const kb::scene::SkeletalMeshLod& lod = mesh.lods[level];
        const double target = 1.0 / static_cast<double>(1U << level);
        const double share = static_cast<double>(lod.indices.size() / 3U) / static_cast<double>(finest);
        shares += " " + std::to_string(share);
        Require(std::fabs(share - target) <= target * 0.1, "A generated level is not within ten percent of its triangle target");
        std::uint64_t unitSumFailures = 0U;
        for (const kb::scene::SkeletalMeshVertex& vertex : lod.vertices) {
            const float sum = vertex.jointWeights[0] + vertex.jointWeights[1] + vertex.jointWeights[2] + vertex.jointWeights[3];
            if (std::fabs(sum - 1.0F) > 1.0e-4F) ++unitSumFailures;
        }
        Require(unitSumFailures == 0U, "A generated level has vertices whose bone weights do not add up to one");
        for (const kb::scene::SkeletalMeshSection& section : lod.sections) {
            for (const kb::scene::SkeletonBoneId bone : section.boneMap) {
                Require(bone == 1U || bone == 2U, "A generated level uses a bone the skeleton does not have");
            }
        }
        Require(lod.requiredBones == std::vector<kb::scene::SkeletonBoneId>{ 1U, 2U } && !lod.boneBounds.empty(),
            "A generated level lost its required bones or bone bounds");
        Require(lod.vertices.size() <= mesh.lods[level - 1U].vertices.size(), "A generated level holds more vertices than the level above");
        // A level whose error stays under the pixel budget at any size is drawn from the same full-screen coverage as the one above.
        Require(mesh.lods[level - 1U].minScreenCoverage >= lod.minScreenCoverage, "The generated levels' screen coverages do not fall");
        coverages += " " + std::to_string(mesh.lods[level - 1U].minScreenCoverage);
    }
    Require(mesh.lods[0].minScreenCoverage > 0.0F && mesh.lods.back().minScreenCoverage == 0.0F,
        "The finest level must be left below its coverage and the last level must draw at any coverage");

    kb::scene::SkeletalMeshAsset chainOff = original;
    settings.levelCount = 1U;
    Require(bake::BuildSkeletalMeshLods(chainOff, settings) == bake::MeshBakeStatus::Success && chainOff.lods.size() == 1U,
        "One level asked for must leave the mesh without a chain");
    kb::scene::SkeletalMeshAsset small = MakeSkinnedSphereAsset(8U, 8U, 7U, 11U);
    settings.levelCount = 4U;
    Require(bake::BuildSkeletalMeshLods(small, settings) == bake::MeshBakeStatus::Success && small.lods.size() == 1U,
        "A mesh under the triangle threshold must get no chain");
    kb::scene::SkeletalMeshAsset regenerated = mesh;
    Require(bake::BuildSkeletalMeshLods(regenerated, settings) == bake::MeshBakeStatus::Success && regenerated.lods.size() == mesh.lods.size() &&
            regenerated.lods[1].indices == mesh.lods[1].indices,
        "Generating the levels of a mesh that has them did not rebuild the same levels from the finest one");
    kb::scene::SkeletalMeshAsset broken = original;
    broken.lods[0].vertices[0].jointWeights = { 0.0F, 0.0F, 0.0F, 0.0F };
    Require(bake::BuildSkeletalMeshLods(broken, settings) == bake::MeshBakeStatus::UnsupportedSourceShape && broken.lods.size() == 1U,
        "A skeletal mesh that is not valid must be refused and left alone");
    std::printf("skeletal_mesh_lod_chain finest_triangles=%zu levels=%zu shares:%s coverages:%s\n", finest, mesh.lods.size(), shares.c_str(),
        coverages.c_str());
}

[[nodiscard]] std::array<float, 16> TranslationAlongView(float depth) noexcept {
    return { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, depth, 1.0F };
}

// Four levels of a mesh of radius 0.1 that are drawn from coverage 0.5, 0.25 and 0.12 down; at a 90 degree field of
// view the coverage of an instance at a depth is 0.1 / depth.
void RunSkinnedLodSelectionTest() {
    constexpr std::array<float, 4U> thresholds{ 0.5F, 0.25F, 0.12F, 0.0F };
    RenderMeshResource mesh{};
    mesh.vertexFormat = RenderVertexFormat::SkinnedP3N3T4UV2J4W4;
    mesh.indexCount = 24U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.1F };
    for (std::uint32_t level = 0U; level < thresholds.size(); ++level) {
        mesh.sections.push_back(RenderMeshSection{ .indexStart = level * 6U, .indexCount = 6U, .bounds = mesh.bounds,
            .lodLevel = static_cast<std::uint8_t>(level) });
        mesh.lods.push_back(RenderMeshLodDesc{ .firstSection = level, .sectionCount = 1U, .minScreenCoverage = thresholds[level] });
    }
    RenderScene cameraScene;
    static_cast<void>(cameraScene.UpsertCamera(CameraRenderProxyDesc{ .entityId = 1U, .verticalFovDegrees = 90.0F, .primary = true }));
    const std::optional<SceneRenderCamera> camera = cameraScene.BuildPrimaryCamera(1024U, 1024U);
    Require(camera.has_value(), "The skinned LOD selection test could not build a camera");

    const auto instanceAt = [](bool skinned, float depth) {
        SceneRenderMeshInstance instance{ .entityId = 1U, .meshAssetId = 42U, .model = TranslationAlongView(depth) };
        if (skinned) {
            instance.currentSkinningPalette = instance.previousSkinningPalette =
                RenderSkinningPaletteHandle{ .frame = 1U, .firstMatrix = 0U, .matrixCount = 2U };
        }
        return instance;
    };
    std::vector<SceneRenderDrawGroup> groups(1U);
    groups[0].meshAssetId = 42U;
    const auto drawnLevel = [&](MeshPipelineBuildResult& result, bool skinned, float depth) {
        groups[0].instances = { instanceAt(skinned, depth) };
        MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{ .pass = MeshPassType::BaseOpaque, .drawGroups = &groups,
            .resolvedMeshResource = &mesh, .camera = &*camera, .resourceValidation = MeshPipelineResourceValidation::Skip }, result);
        Require(result.commands.size() == 1U && result.commands[0].instances.size() == 1U, "A skinned instance was not drawn once");
        return result.commands[0].lodLevel;
    };

    // A character walking away passes through every level in order, and back through them again.
    MeshPipelineBuildResult result;
    std::uint32_t changes = 0U;
    std::uint32_t levelsSeen = 0U;
    std::uint8_t previous = drawnLevel(result, true, 0.15F);
    Require(previous == 0U, "A character close to the camera must draw its finest level");
    for (float depth = 0.15F; depth <= 1.3F; depth += 0.005F) {
        const std::uint8_t level = drawnLevel(result, true, depth);
        Require(level >= previous, "A character walking away went back to a finer level");
        levelsSeen |= 1U << level;
        if (level != previous) ++changes;
        previous = level;
        // The level changes only past the threshold, never short of it.
        Require(level == 0U || 0.1F / depth < thresholds[level - 1U], "A level was left before its threshold");
    }
    Require(levelsSeen == 0xFU && changes == 3U && previous == 3U, "A character walking away did not pass through levels 0, 1, 2 and 3 once each");
    std::uint32_t changesBack = 0U;
    for (float depth = 1.3F; depth >= 0.15F; depth -= 0.005F) {
        const std::uint8_t level = drawnLevel(result, true, depth);
        Require(level <= previous, "A character walking closer went back to a coarser level");
        if (level != previous) ++changesBack;
        previous = level;
        Require(level == 3U || 0.1F / depth >= thresholds[level], "A level was kept past its threshold on the way back");
    }
    Require(changesBack == 3U && previous == 0U, "A character walking closer did not pass through every level once");

    // A character standing on a threshold, its size moving two percent either way each frame, keeps its level; the
    // same size without a history changes level every frame.
    constexpr std::uint32_t kFrames = 40U;
    std::uint32_t historyChanges = 0U;
    std::uint32_t statelessChanges = 0U;
    for (std::size_t threshold = 0U; threshold < 3U; ++threshold) {
        for (const float start : { 0.9F, 1.1F }) {
            MeshPipelineBuildResult held;
            std::uint8_t last = drawnLevel(held, true, 0.1F / (thresholds[threshold] * start));
            std::uint8_t stateless = last;
            for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
                const float depth = 0.1F / (thresholds[threshold] * (frame % 2U == 0U ? 1.02F : 0.98F));
                const std::uint8_t level = drawnLevel(held, true, depth);
                if (level != last) ++historyChanges;
                last = level;
                const std::uint8_t plain = MeshPipelineVisibility::SelectLodLevel(&mesh, instanceAt(true, depth), &*camera);
                if (plain != stateless) ++statelessChanges;
                stateless = plain;
            }
        }
    }
    Require(historyChanges == 0U, "A skinned mesh changed level while its size moved two percent around a threshold");
    Require(statelessChanges >= kFrames * 6U - 6U, "The oscillation does not cross the threshold, so it proves no hysteresis");
    // A mesh that is not skinned keeps choosing its level from the size alone.
    MeshPipelineBuildResult rigid;
    std::uint32_t rigidChanges = 0U;
    std::uint8_t rigidLast = drawnLevel(rigid, false, 0.1F / (thresholds[0] * 1.02F));
    for (std::uint32_t frame = 0U; frame < kFrames; ++frame) {
        const std::uint8_t level = drawnLevel(rigid, false, 0.1F / (thresholds[0] * (frame % 2U == 0U ? 0.98F : 1.02F)));
        if (level != rigidLast) ++rigidChanges;
        rigidLast = level;
    }
    Require(rigidChanges == kFrames, "A mesh that is not skinned must keep its stateless level choice");
    std::printf("skinned_lod_selection changes_walking_away=%u changes_walking_closer=%u oscillation_changes_with_history=%u "
                "oscillation_changes_without=%u frames_per_run=%u\n", changes, changesBack, historyChanges, statelessChanges, kFrames);
}

} // namespace

void RunSkinnedMeshLodChainTests() {
    RunSkeletalMeshLodChainTest();
    RunSkinnedLodSelectionTest();
}

} // namespace kb::render::tests
