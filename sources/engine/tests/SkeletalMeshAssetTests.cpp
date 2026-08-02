#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshGltfImporter.hpp"
#include "engine/scene/SkeletalMeshGltfImportPlanner.hpp"
#include "engine/scene/SkeletalMeshGltfImportPublisher.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void ReplaceOnce(std::string& text, std::string_view needle, std::string_view replacement) {
    const std::size_t offset = text.find(needle);
    if (offset == std::string::npos) throw std::runtime_error{ "Skeletal glTF fixture replacement failed" };
    text.replace(offset, needle.size(), replacement);
}

void WriteSkeletalGltfFixture(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    std::ofstream binary{ folder / "Hero.bin", std::ios::binary | std::ios::trunc };
    const std::array<float, 9U> positions{ 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F };
    const std::array<std::uint16_t, 12U> joints{ 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U };
    const std::array<float, 12U> weights{ 2.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F, 0.0F };
    const std::array<std::uint16_t, 3U> indices{ 0U, 1U, 2U };
    const std::array<float, 32U> inverseBinds{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F,
    };
    binary.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
    binary.write(reinterpret_cast<const char*>(joints.data()), sizeof(joints));
    binary.write(reinterpret_cast<const char*>(weights.data()), sizeof(weights));
    binary.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
    binary.write(reinterpret_cast<const char*>(inverseBinds.data()), sizeof(inverseBinds));
    binary.close();
    WriteTextFile(folder / "Hero.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"Hero.bin","byteLength":242}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":6},{"buffer":0,"byteOffset":114,"byteLength":128}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],"nodes":[{"name":"Root","children":[1]},{"name":"Spine","translation":[0,1,0]},{"mesh":0,"skin":0}],"skins":[{"joints":[0,1],"inverseBindMatrices":4}]})");
}

std::uint64_t ResolveFixtureMaterial(std::string_view name, void*) {
    return name == "Skin" ? 31337U : 0U;
}

void WriteExtendedSkeletalGltfFixture(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    std::ofstream binary{ folder / "HeroExtended.bin", std::ios::binary | std::ios::trunc };
    const std::array<float, 9U> positions{ 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 2.0F };
    const std::array<std::uint16_t, 12U> joints{ 0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U };
    const std::array<float, 12U> weights{ 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F };
    const std::array<std::uint16_t, 3U> indices{ 0U, 1U, 2U };
    const std::array<float, 32U> inverseBinds{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F, 0.0F, 1.0F,
    };
    const std::array<float, 9U> morphPositionDeltas{ 0.0F, 0.0F, 0.25F, 0.0F, 0.0F, 0.25F, 0.0F, 0.0F, 0.25F };
    const std::array<float, 2U> times{ 0.0F, 1.0F };
    const std::array<float, 6U> spineTranslations{ 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 2.0F };
    const std::array<float, 2U> morphWeights{ 0.0F, 1.0F };
    binary.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
    binary.write(reinterpret_cast<const char*>(joints.data()), sizeof(joints));
    binary.write(reinterpret_cast<const char*>(weights.data()), sizeof(weights));
    binary.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
    binary.write(reinterpret_cast<const char*>(inverseBinds.data()), sizeof(inverseBinds));
    binary.write(reinterpret_cast<const char*>(morphPositionDeltas.data()), sizeof(morphPositionDeltas));
    binary.write(reinterpret_cast<const char*>(times.data()), sizeof(times));
    binary.write(reinterpret_cast<const char*>(spineTranslations.data()), sizeof(spineTranslations));
    binary.write(reinterpret_cast<const char*>(morphWeights.data()), sizeof(morphWeights));
    binary.close();
    WriteTextFile(folder / "HeroExtended.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"HeroExtended.bin","byteLength":318}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":6},{"buffer":0,"byteOffset":114,"byteLength":128},{"buffer":0,"byteOffset":242,"byteLength":36},{"buffer":0,"byteOffset":278,"byteLength":8},{"buffer":0,"byteOffset":286,"byteLength":24},{"buffer":0,"byteOffset":310,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},{"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"},{"bufferView":5,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":6,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":7,"componentType":5126,"count":2,"type":"VEC3"},{"bufferView":8,"componentType":5126,"count":2,"type":"SCALAR"}],"materials":[{"name":"Skin"}],"meshes":[{"extras":{"targetNames":["Smile"]},"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3,"material":0,"targets":[{"POSITION":5}]}]}],"nodes":[{"name":"Root","children":[1]},{"name":"Spine","translation":[0,1,0]},{"mesh":0,"skin":0}],"skins":[{"joints":[0,1],"inverseBindMatrices":4}],"animations":[{"samplers":[{"input":6,"output":7,"interpolation":"LINEAR"},{"input":6,"output":8,"interpolation":"LINEAR"}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},{"sampler":1,"target":{"node":2,"path":"weights"}}]}]})");
}

} // namespace

namespace kb::tests {

void RunSkeletalMeshAssetTests() {
    const auto root=std::filesystem::temp_directory_path()/"21kb-skeletal-mesh-asset-tests";
    std::filesystem::remove_all(root); const auto path=root/"Assets/Characters/Hero.kbskeletalmesh";
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {
        { .id = 1U, .parentIndex = -1, .name = "Root", .referencePose = {}, .inverseBind = {} },
        { .id = 2U, .parentIndex = 0, .name = "Spine", .referencePose = { .position = { 0.0F, 1.0F, 0.0F } }, .inverseBind = {} },
    };
    const auto skeletonPath = root / "Assets/Characters/Hero.kbskeleton";
    Require(kb::scene::SkeletonAssetIO::Save(skeletonPath, skeleton),
        "SkeletalMeshAsset test Skeleton could not be saved");
    kb::scene::SkeletalMeshAsset mesh{};
    mesh.skeletonAssetId = 1U;
    mesh.skeletonCompatibilitySignature =
        kb::scene::SkeletonCompatibilitySignature(skeleton);
    mesh.conservativeBounds={.center={0,0,0},.extents={1,2,1}};
    kb::scene::SkeletalMeshLod lod{}; lod.requiredBones={1U,2U}; lod.minScreenCoverage=0.5F;
    lod.vertices={ {}, {.position={1,0,0}}, {.position={0,1,0}} }; lod.indices={0,1,2}; lod.sections={{.firstIndex=0,.indexCount=3,.materialAssetId=42U,.boneMap={1U,2U}}}; mesh.lods={lod};
    mesh.morphTargets={{.name="Smile",.lodIndex=0,.deltas={{.vertexIndex=1,.positionDelta={0,0.1F,0}}}}};
    Require(kb::scene::ValidateSkeletalMeshAsset(mesh).valid,"Valid SkeletalMeshAsset was rejected");
    Require(kb::scene::SkeletalMeshAssetIO::Save(path,mesh),"SkeletalMeshAsset production save failed");
    const auto loaded=kb::scene::SkeletalMeshAssetIO::Load(path);
    Require(loaded && loaded->lods.size()==1U && loaded->lods[0].sections[0].materialAssetId==42U && loaded->morphTargets[0].deltas.size()==1U,"SkeletalMeshAsset round trip lost canonical data");
    const auto deterministicPath = root / "RoundTrip/HeroCopy.kbskeletalmesh";
    Require(kb::scene::SkeletalMeshAssetIO::Save(deterministicPath, mesh) &&
            ReadTextFile(path) == ReadTextFile(deterministicPath),
        "SkeletalMeshAsset serialization is not deterministic");
    const std::string serialized = ReadTextFile(path);
    const std::size_t headerEnd = serialized.find('\n');
    Require(headerEnd != std::string::npos,
        "SkeletalMeshAsset serialization has no schema header");
    const auto legacyPath = root / "RoundTrip/Legacy.kbskeletalmesh";
    WriteTextFile(legacyPath, std::string_view{ serialized }.substr(headerEnd + 1U));
    Require(kb::scene::SkeletalMeshAssetIO::Load(legacyPath).has_value(),
        "SkeletalMeshAsset legacy migration failed");
    const auto corruptPath = root / "RoundTrip/Corrupt.kbskeletalmesh";
    WriteTextFile(corruptPath, "21kb SkeletalMesh 99\n");
    std::string corruptError;
    Require(!kb::scene::SkeletalMeshAssetIO::Load(corruptPath, &corruptError).has_value() &&
            corruptError.find("line 1") != std::string::npos,
        "SkeletalMeshAsset accepted an unsupported schema version without a diagnostic");
    kb::scene::SkeletalMeshAsset invalid=mesh; invalid.lods[0].vertices[0].jointWeights={0,0,0,0};
    Require(!kb::scene::ValidateSkeletalMeshAsset(invalid).valid,"SkeletalMeshAsset accepted zero skin weights");
    const auto importRoot = root / "GltfImport";
    WriteSkeletalGltfFixture(importRoot);
    std::string importError;
    kb::scene::SkeletalMeshGltfImportReport importReport;
    const auto imported = kb::scene::SkeletalMeshGltfImporter::Import(
        importRoot / "Hero.gltf", 777U, &importError, &importReport);
    Require(imported.has_value() && imported->skeleton.bones.size() == 2U &&
            imported->skeleton.bones[1].parentIndex == 0 &&
            imported->mesh.skeletonAssetId == 777U &&
            imported->mesh.lods.size() == 1U &&
            imported->mesh.lods[0].vertices.size() == 3U &&
            imported->mesh.lods[0].vertices[1].jointIndices[0] == 1U &&
            imported->mesh.lods[0].vertices[1].jointWeights[0] == 1.0F &&
            kb::scene::ValidateSkeletalMeshAsset(imported->mesh).valid,
        "Skeletal glTF import did not preserve hierarchy, inverse binds, JOINTS_0, and WEIGHTS_0");
    Require(!importReport.HasErrors() && importReport.diagnostics.size() == 2U &&
            importReport.diagnostics[0].severity == kb::scene::SkeletalMeshGltfImportDiagnosticSeverity::Warning &&
            importReport.diagnostics[0].primitiveIndex == 0,
        "Skeletal glTF import did not report defaulted vertex attributes");
    const auto plannerRoot = root / "GltfImportPlanner";
    std::filesystem::create_directories(plannerRoot / "Assets" / "Characters");
    kb::scene::Scene plannerScene;
    Require(plannerScene.Assets().MountProject(plannerRoot),
        "Skeletal glTF import planner mount failed");
    const auto createPlan = kb::scene::SkeletalMeshGltfImportPlanner::Plan(
        plannerScene.Assets().Manager(), importRoot / "Hero.gltf", "/Game/Characters", {}, &importError);
    Require(createPlan.has_value() && !createPlan->reusesSkeleton &&
            createPlan->skeletonVirtualPath == "/Game/Characters/Hero.kbskeleton" &&
            createPlan->imported.mesh.skeletonAssetId == createPlan->skeletonAssetId.value,
        "Skeletal glTF import planner did not deterministically plan a new Skeleton asset");
    Require(kb::scene::SkeletonAssetIO::Save(
                plannerRoot / "Assets" / "Characters" / "Shared.kbskeleton", imported->skeleton) &&
            plannerScene.Assets().Discover() == 1U,
        "Skeletal glTF import planner could not register a compatible Skeleton asset");
    const auto reusePlan = kb::scene::SkeletalMeshGltfImportPlanner::Plan(
        plannerScene.Assets().Manager(), importRoot / "Hero.gltf", "/Game/Characters", {}, &importError);
    Require(reusePlan.has_value() && reusePlan->reusesSkeleton &&
            reusePlan->skeletonVirtualPath == "/Game/Characters/Shared.kbskeleton" &&
            reusePlan->imported.mesh.skeletonAssetId == reusePlan->skeletonAssetId.value,
        "Skeletal glTF import planner did not reuse a compatible Skeleton asset");
    const auto published = kb::scene::SkeletalMeshGltfImportPublisher::Publish(
        plannerScene.Assets().Manager(), *reusePlan, &importError);
    Require(published.has_value() && !published->createdSkeleton &&
            published->skeletonAssetId == reusePlan->skeletonAssetId && published->meshAssetId.IsValid(),
        "Skeletal glTF import publisher did not atomically publish a compatible Skeleton reuse plan");
    const auto publishedMeshPath = plannerRoot / "Assets" / "Characters" / "Hero.kbskeletalmesh";
    const std::string publishedMeshBytes = ReadTextFile(publishedMeshPath);
    Require(!publishedMeshBytes.empty() &&
            kb::scene::SkeletalMeshAssetIO::Load(publishedMeshPath)->skeletonAssetId == reusePlan->skeletonAssetId.value,
        "Skeletal glTF import publisher did not preserve the planned Skeleton reference");
    Require(!kb::scene::SkeletalMeshGltfImportPlanner::Plan(
                plannerScene.Assets().Manager(), plannerRoot / "Missing.gltf", "/Game/Characters", {}, &importError).has_value() &&
            ReadTextFile(publishedMeshPath) == publishedMeshBytes,
        "A failed skeletal glTF reimport modified the last valid mesh asset");
    std::fstream reimportBuffer{ importRoot / "Hero.bin", std::ios::in | std::ios::out | std::ios::binary };
    const float reimportPosition = 2.0F;
    reimportBuffer.seekp(0, std::ios::beg);
    reimportBuffer.write(reinterpret_cast<const char*>(&reimportPosition), sizeof(reimportPosition));
    reimportBuffer.close();
    const auto reimportPlan = kb::scene::SkeletalMeshGltfImportPlanner::Plan(
        plannerScene.Assets().Manager(), importRoot / "Hero.gltf", "/Game/Characters", {}, &importError);
    const auto reimported = reimportPlan ? kb::scene::SkeletalMeshGltfImportPublisher::Publish(
        plannerScene.Assets().Manager(), *reimportPlan, &importError) : std::nullopt;
    Require(reimported.has_value() && reimported->meshAssetId == published->meshAssetId &&
            kb::scene::SkeletalMeshAssetIO::Load(publishedMeshPath)->lods[0].vertices[0].position.x == 2.0F,
        "Skeletal glTF reimport did not retain the mesh reference while publishing new content");
    const auto extendedImportRoot = root / "GltfExtendedImport";
    WriteExtendedSkeletalGltfFixture(extendedImportRoot);
    kb::scene::SkeletalMeshGltfImportOptions extendedOptions{};
    extendedOptions.coordinateConversion.unitScale = 100.0F;
    extendedOptions.materialResolver = ResolveFixtureMaterial;
    const auto extendedImported = kb::scene::SkeletalMeshGltfImporter::Import(
        extendedImportRoot / "HeroExtended.gltf", 777U, extendedOptions, &importError);
    const std::string extendedImportFailure =
        "Skeletal glTF import rejected channels, morphs, material sections, or coordinate conversion: " + importError;
    Require(extendedImported.has_value(), extendedImportFailure.c_str());
    Require(extendedImported->mesh.lods[0].sections[0].materialAssetId == 31337U,
        "Skeletal glTF import did not resolve material section");
    Require(extendedImported->mesh.lods[0].indices[1] == 2U,
        "Skeletal glTF import did not reverse triangle winding for handedness conversion");
    Require(extendedImported->mesh.lods[0].vertices[2].position.z == -200.0F,
        "Skeletal glTF import did not convert source units or handedness");
    Require(extendedImported->mesh.morphTargets.size() == 1U &&
            extendedImported->mesh.morphTargets[0].name == "Smile" &&
            extendedImported->mesh.morphTargets[0].deltas[0].positionDelta.z == -25.0F,
        "Skeletal glTF import did not preserve morph target deltas");
    Require(kb::scene::ValidateSkeletalMeshAsset(extendedImported->mesh).valid,
        "Skeletal glTF import produced an invalid mesh after conversion");
    Require(extendedImported->clips.size() == 1U &&
            extendedImported->clips[0].durationSeconds == 1.0F &&
            extendedImported->clips.size() == 1U &&
            extendedImported->clips[0].skeletalTracks.size() == 1U &&
            extendedImported->clips[0].skeletalTracks[0].keyframes[1].transform.position.z == -200.0F &&
            extendedImported->clips[0].morphTracks.size() == 1U &&
            extendedImported->clips[0].morphTracks[0].keyframes[1].weight == 1.0F,
        "Skeletal glTF import did not preserve animation channels or morph weights");
    Require(kb::scene::AnimationAssetIO::SaveClip(
                extendedImportRoot / "HeroImported.kbanim", extendedImported->clips[0]),
        "Skeletal glTF importer produced an animation clip that cannot be serialized");
    const auto zeroWeightRoot = root / "GltfZeroWeight";
    WriteSkeletalGltfFixture(zeroWeightRoot);
    std::fstream zeroWeightBuffer{
        zeroWeightRoot / "Hero.bin", std::ios::in | std::ios::out | std::ios::binary };
    const std::array<float, 4U> zeroWeights{};
    zeroWeightBuffer.seekp(60, std::ios::beg);
    zeroWeightBuffer.write(reinterpret_cast<const char*>(zeroWeights.data()), sizeof(zeroWeights));
    zeroWeightBuffer.close();
    kb::scene::SkeletalMeshGltfImportReport zeroWeightReport;
    Require(!kb::scene::SkeletalMeshGltfImporter::Import(
                zeroWeightRoot / "Hero.gltf", 777U, &importError, &zeroWeightReport).has_value() &&
            importError.find("zero-weight") != std::string::npos && zeroWeightReport.HasErrors() &&
            zeroWeightReport.diagnostics.front().primitiveIndex == 0 &&
            !zeroWeightReport.diagnostics.front().mesh.empty() &&
            !zeroWeightReport.diagnostics.front().node.empty(),
        "Skeletal glTF import accepted a zero-weight binding");
    const auto invalidMatrixRoot = root / "GltfInvalidMatrix";
    WriteSkeletalGltfFixture(invalidMatrixRoot);
    std::fstream invalidMatrixBuffer{ invalidMatrixRoot / "Hero.bin", std::ios::in | std::ios::out | std::ios::binary };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    invalidMatrixBuffer.seekp(114, std::ios::beg);
    invalidMatrixBuffer.write(reinterpret_cast<const char*>(&nan), sizeof(nan));
    invalidMatrixBuffer.close();
    Require(!kb::scene::SkeletalMeshGltfImporter::Import(
                invalidMatrixRoot / "Hero.gltf", 777U, &importError).has_value(),
        "Skeletal glTF import accepted a non-finite inverse bind matrix");
    const auto missingJointRoot = root / "GltfMissingJoint";
    WriteSkeletalGltfFixture(missingJointRoot);
    std::fstream missingJointBuffer{ missingJointRoot / "Hero.bin", std::ios::in | std::ios::out | std::ios::binary };
    const std::uint16_t missingJoint = 2U;
    missingJointBuffer.seekp(36, std::ios::beg);
    missingJointBuffer.write(reinterpret_cast<const char*>(&missingJoint), sizeof(missingJoint));
    missingJointBuffer.close();
    Require(!kb::scene::SkeletalMeshGltfImporter::Import(
                missingJointRoot / "Hero.gltf", 777U, &importError).has_value() &&
            importError.find("invalid JOINTS_0") != std::string::npos,
        "Skeletal glTF import accepted a missing skin joint");
    const auto multipleSkinsRoot = root / "GltfMultipleSkins";
    WriteSkeletalGltfFixture(multipleSkinsRoot);
    std::string multipleSkins = ReadTextFile(multipleSkinsRoot / "Hero.gltf");
    ReplaceOnce(multipleSkins, "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":4}]",
        "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":4},{\"joints\":[0,1],\"inverseBindMatrices\":4}]");
    WriteTextFile(multipleSkinsRoot / "Hero.gltf", multipleSkins);
    Require(!kb::scene::SkeletalMeshGltfImporter::Import(
                multipleSkinsRoot / "Hero.gltf", 777U, &importError).has_value() &&
            importError.find("exactly one") != std::string::npos,
        "Skeletal glTF import accepted multiple skins");
    const auto cyclicHierarchyRoot = root / "GltfCyclicHierarchy";
    WriteSkeletalGltfFixture(cyclicHierarchyRoot);
    std::string cyclicHierarchy = ReadTextFile(cyclicHierarchyRoot / "Hero.gltf");
    ReplaceOnce(cyclicHierarchy, "\"children\":[1]", "\"children\":[0,1]");
    WriteTextFile(cyclicHierarchyRoot / "Hero.gltf", cyclicHierarchy);
    Require(!kb::scene::SkeletalMeshGltfImporter::Import(
                cyclicHierarchyRoot / "Hero.gltf", 777U, &importError).has_value(),
        "Skeletal glTF import accepted a cyclic joint hierarchy");
    kb::scene::Scene scene; Require(scene.Assets().MountProject(root),"SkeletalMeshAsset mount failed"); Require(scene.Assets().Discover()==2U,"SkeletalMeshAsset discovery failed");
    const auto* skeletonMetadata = scene.Assets().Manager().Registry().FindByPath(
        "/Game/Characters/Hero.kbskeleton");
    Require(skeletonMetadata != nullptr,
        "SkeletalMeshAsset Skeleton registry classification failed");
    const kb::assets::AssetId skeletonId = skeletonMetadata->id;
    const kb::scene::SceneObject bindingObject = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Skeletal binding" });
    const kb::scene::SkeletonBindingComponent binding{
        .skeletonAssetId = skeletonId.value,
        .skeletonCompatibilitySignature = kb::scene::SkeletonCompatibilitySignature(skeleton),
        .enabled = true,
    };
    Require(kb::scene::IsSkeletonBindingComponentValid(binding),
        "Skeleton binding requires an authoritative skeleton asset and compatibility signature");
    Require(scene.Components().SkeletonBindings().Set(bindingObject.Entity(), binding),
        "Skeleton binding component rejected valid authoritative configuration");
    const auto* storedBinding = scene.Components().SkeletonBindings().TryGet(bindingObject.Entity());
    Require(storedBinding != nullptr && storedBinding->skeletonAssetId == binding.skeletonAssetId &&
            storedBinding->skeletonCompatibilitySignature == binding.skeletonCompatibilitySignature,
        "Skeleton binding component did not preserve its authoritative configuration");
    Require(!scene.Components().SkeletonBindings().Set(bindingObject.Entity(), {
                .skeletonAssetId = binding.skeletonAssetId,
                .skeletonCompatibilitySignature = 0U,
                .enabled = true,
            }),
        "Skeleton binding accepted an incomplete authoritative configuration");
    Require(scene.Components().SkeletonBindings().TryGet(bindingObject.Entity())->skeletonAssetId == binding.skeletonAssetId,
        "Skeleton binding mutation changed the last valid authoritative configuration");
    scene.Components().SkeletonBindings().Remove(bindingObject.Entity());
    Require(!scene.Components().SkeletonBindings().Has(bindingObject.Entity()),
        "Skeleton binding component was not removed from its live entity");
    const kb::scene::SceneObject deformedGeometryObject = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "Deformed geometry" });
    const kb::scene::DrawD3DeformedGeometryComponent deformedGeometry{
        .skeletalMeshAssetId = 999U,
        .materialSlotAssetIds = { 41U },
        .materialSlotOverrideCount = 1U,
        .poseSource = bindingObject.Entity(),
        .lodBias = -1,
        .lodEnabled = true,
        .fixedBounds = true,
        .castsShadow = true,
        .receivesShadow = true,
        .layer = 4U,
        .enabled = true,
    };
    Require(kb::scene::IsDrawD3DeformedGeometryComponentValid(deformedGeometry) &&
            scene.Components().DeformedGeometries().Set(deformedGeometryObject.Entity(), deformedGeometry),
        "Deformed geometry component rejected valid authored configuration");
    const auto* storedDeformedGeometry = scene.Components().DeformedGeometries().TryGet(deformedGeometryObject.Entity());
    Require(storedDeformedGeometry != nullptr && storedDeformedGeometry->poseSource == bindingObject.Entity() &&
            storedDeformedGeometry->fixedBounds && storedDeformedGeometry->materialSlotAssetIds[0] == 41U,
        "Deformed geometry component did not preserve its mesh, material, bounds, and pose-source configuration");
    kb::scene::DrawD3DeformedGeometryComponent invalidDeformedGeometry = deformedGeometry;
    invalidDeformedGeometry.materialSlotOverrideCount = kb::scene::kMaxDeformedGeometryMaterialSlotOverrides + 1U;
    Require(!scene.Components().DeformedGeometries().Set(deformedGeometryObject.Entity(), invalidDeformedGeometry),
        "Deformed geometry accepted an invalid material override range");
    scene.Components().DeformedGeometries().Remove(deformedGeometryObject.Entity());
    Require(!scene.Components().DeformedGeometries().Has(deformedGeometryObject.Entity()),
        "Deformed geometry component was not removed from its live entity");
    mesh.skeletonAssetId = skeletonId.value;
    Require(kb::scene::SkeletalMeshAssetIO::Save(path, mesh),
        "SkeletalMeshAsset could not store the canonical Skeleton id");
    Require(scene.Assets().Discover() == 2U,
        "SkeletalMeshAsset reference reimport discovery failed");
    const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Characters/Hero.kbskeletalmesh");
    Require(metadata && metadata->type==kb::scene::kSkeletalMeshAssetType,"SkeletalMeshAsset registry classification failed");
    const auto runtime=scene.Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(metadata->id);
    Require(runtime.IsLoaded() && runtime->skeletonAssetId == skeletonId.value && runtime->lods[0].vertices.size()==3U,"SkeletalMeshAsset loader did not produce canonical runtime data");
    Require(scene.Assets().Manager().ValidateCompatibility(metadata->id).compatible,
        "SkeletalMeshAsset rejected a matching Skeleton reference");
    skeleton.bones[1].referencePose.position.y = 2.0F;
    Require(kb::scene::SkeletonAssetIO::Save(skeletonPath, skeleton),
        "SkeletalMeshAsset test Skeleton reimport could not be saved");
    Require(scene.Assets().Discover() == 2U,
        "SkeletalMeshAsset reimport discovery failed");
    const auto report = scene.Assets().Manager().ValidateCompatibility(metadata->id);
    Require(!report.compatible && !report.diagnostics.empty() &&
            report.diagnostics.front().issue == kb::assets::AssetCompatibilityIssue::IncompatibleDependency,
        "SkeletalMeshAsset accepted a reimported incompatible Skeleton");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
