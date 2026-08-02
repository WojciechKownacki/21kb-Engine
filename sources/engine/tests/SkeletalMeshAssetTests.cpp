#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <filesystem>

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
    kb::scene::SkeletalMeshAsset invalid=mesh; invalid.lods[0].vertices[0].jointWeights={0,0,0,0};
    Require(!kb::scene::ValidateSkeletalMeshAsset(invalid).valid,"SkeletalMeshAsset accepted zero skin weights");
    kb::scene::Scene scene; Require(scene.Assets().MountProject(root),"SkeletalMeshAsset mount failed"); Require(scene.Assets().Discover()==2U,"SkeletalMeshAsset discovery failed");
    const auto* skeletonMetadata = scene.Assets().Manager().Registry().FindByPath(
        "/Game/Characters/Hero.kbskeleton");
    Require(skeletonMetadata != nullptr,
        "SkeletalMeshAsset Skeleton registry classification failed");
    const kb::assets::AssetId skeletonId = skeletonMetadata->id;
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
