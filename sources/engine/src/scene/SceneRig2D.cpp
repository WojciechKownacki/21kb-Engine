#include "engine/scene/Rig2DAsset.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAnimatorComponents.hpp"
#include "engine/scene/SceneSkeletalMeshSetup.hpp"
#include "engine/scene/SceneDeformedGeometryComponents.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include <cmath>

namespace kb::scene {
bool AttachRig2D(Scene& scene,SceneEntity owner,std::string_view meshPath,std::string_view controllerPath,std::string& error) {
    error.clear();if(!scene.Entities().IsAlive(owner)) {error="rig2d owner is not alive";return false;}
    auto& assets=scene.Assets().Manager();
    const auto* meshInfo=assets.Registry().FindByPath(kb::assets::Utf8Path(meshPath));
    const auto* controllerInfo=assets.Registry().FindByPath(kb::assets::Utf8Path(controllerPath));
    if(!meshInfo||meshInfo->type!=kSkeletalMeshAssetType||!controllerInfo||controllerInfo->type!="AnimatorController") {error="rig2d requires saved skeletal mesh and controller assets";return false;}
    const auto mesh=assets.Load<SkeletalMeshAsset>(meshInfo->id);if(!mesh.IsLoaded()) {error=assets.LastError();return false;}
    const auto controller=assets.Load<AnimatorController>(controllerInfo->id);if(!controller.IsLoaded()) {error=assets.LastError();return false;}
    for(const auto& lod:mesh.Shared()->lods) for(const auto& vertex:lod.vertices) if(std::abs(vertex.position.z)>.00001F) {error="rig2d mesh must lie in local XY";return false;}
    const auto attached=SceneSkeletalMeshSetup::Attach(scene,owner,meshInfo->id);if(!attached.succeeded) {error=attached.error;return false;}
    scene.Components().Animators().Set(owner,Animator{.controllerAssetId=controllerInfo->id.value});
    auto geometry=*scene.Components().DeformedGeometries().TryGet(owner);geometry.castsShadow=false;geometry.receivesShadow=false;
    static_cast<void>(scene.Components().DeformedGeometries().Set(owner,geometry));return true;
}
}
