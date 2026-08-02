#include "TestSupport.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <filesystem>

namespace kb::tests {

void RunSkeletalMeshAssetTests() {
    const auto root=std::filesystem::temp_directory_path()/"21kb-skeletal-mesh-asset-tests";
    std::filesystem::remove_all(root); const auto path=root/"Assets/Characters/Hero.kbskeletalmesh";
    kb::scene::SkeletalMeshAsset mesh{}; mesh.skeletonAssetId=101U; mesh.skeletonCompatibilitySignature=202U; mesh.conservativeBounds={.center={0,0,0},.extents={1,2,1}};
    kb::scene::SkeletalMeshLod lod{}; lod.requiredBones={1U,2U}; lod.minScreenCoverage=0.5F;
    lod.vertices={ {}, {.position={1,0,0}}, {.position={0,1,0}} }; lod.indices={0,1,2}; lod.sections={{.firstIndex=0,.indexCount=3,.materialAssetId=42U,.boneMap={1U,2U}}}; mesh.lods={lod};
    mesh.morphTargets={{.name="Smile",.lodIndex=0,.deltas={{.vertexIndex=1,.positionDelta={0,0.1F,0}}}}};
    Require(kb::scene::ValidateSkeletalMeshAsset(mesh).valid,"Valid SkeletalMeshAsset was rejected");
    Require(kb::scene::SkeletalMeshAssetIO::Save(path,mesh),"SkeletalMeshAsset production save failed");
    const auto loaded=kb::scene::SkeletalMeshAssetIO::Load(path);
    Require(loaded && loaded->lods.size()==1U && loaded->lods[0].sections[0].materialAssetId==42U && loaded->morphTargets[0].deltas.size()==1U,"SkeletalMeshAsset round trip lost canonical data");
    kb::scene::SkeletalMeshAsset invalid=mesh; invalid.lods[0].vertices[0].jointWeights={0,0,0,0};
    Require(!kb::scene::ValidateSkeletalMeshAsset(invalid).valid,"SkeletalMeshAsset accepted zero skin weights");
    kb::scene::Scene scene; Require(scene.Assets().MountProject(root),"SkeletalMeshAsset mount failed"); Require(scene.Assets().Discover()==1U,"SkeletalMeshAsset discovery failed");
    const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/Characters/Hero.kbskeletalmesh");
    Require(metadata && metadata->type==kb::scene::kSkeletalMeshAssetType,"SkeletalMeshAsset registry classification failed");
    const auto runtime=scene.Assets().Manager().Load<kb::scene::SkeletalMeshAsset>(metadata->id);
    Require(runtime.IsLoaded() && runtime->skeletonAssetId==101U && runtime->lods[0].vertices.size()==3U,"SkeletalMeshAsset loader did not produce canonical runtime data");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
