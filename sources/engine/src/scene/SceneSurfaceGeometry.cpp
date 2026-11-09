#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include <algorithm>
#include <cmath>

namespace kb::scene {
bool SceneSurfaceGeometry::Publish(Scene& scene,SceneEntity owner,std::span<const SurfaceGeometryVertex> vertices,
    std::span<const std::uint32_t> indices,std::string& error) {
    error.clear();
    if(!scene.Entities().IsAlive(owner)||vertices.size()<3||vertices.size()>65536||indices.empty()||indices.size()%3!=0||indices.size()>393216) {
        error="invalid surface owner or triangle geometry size";return false;
    }
    for(const auto& vertex:vertices) for(float v:{vertex.position.x,vertex.position.y,vertex.position.z,vertex.normal.x,vertex.normal.y,vertex.normal.z,vertex.u,vertex.v})
        if(!std::isfinite(v)) {error="surface vertices must be finite";return false;}
    if(std::any_of(indices.begin(),indices.end(),[&](auto index) {return index>=vertices.size();})) {error="surface index is out of range";return false;}
    auto& surfaces=SceneAccess::State(scene).surfaceGeometries;
    std::size_t vertexBudget=vertices.size(),indexBudget=indices.size();
    for(const auto& entry:surfaces) if(entry.owner!=owner) {vertexBudget+=entry.vertices.size();indexBudget+=entry.indices.size();}
    if(vertexBudget>262144||indexBudget>1572864) {error="scene surface geometry budget exceeded";return false;}

    auto found=std::find_if(surfaces.begin(),surfaces.end(),[&](const auto& entry) {return entry.owner==owner;});
    if(found==surfaces.end()) {
        if(surfaces.size()>=256) {error="scene surface geometry budget exceeded";return false;}
        SurfaceGeometry surface;surface.owner=owner;
        surface.meshAssetId=kb::assets::MakeAssetId("/Runtime/Surfaces/"+std::to_string(scene.Id())+"/"+std::to_string(owner.Id())+":RenderMesh").value;
        surfaces.push_back(std::move(surface));found=surfaces.end()-1;
    }
    found->vertices.assign(vertices.begin(),vertices.end());found->indices.assign(indices.begin(),indices.end());found->revision=SceneAccess::State(scene).nextSurfaceGeometryRevision++;
    MeshRendererComponent renderer;
    if(const auto* previous=scene.Components().MeshRenderers().TryGet(owner)) renderer=*previous;
    renderer.meshAssetId=found->meshAssetId;renderer.transient=true;scene.Components().MeshRenderers().Set(owner,renderer);
    return true;
}
std::span<const SurfaceGeometry> SceneSurfaceGeometry::All(const Scene& scene) noexcept {return SceneAccess::State(scene).surfaceGeometries;}
void SceneSurfaceGeometry::Remove(Scene& scene,SceneEntity owner) noexcept {
    auto& surfaces=SceneAccess::State(scene).surfaceGeometries;
    const auto found=std::find_if(surfaces.begin(),surfaces.end(),[&](const auto& value) {return value.owner==owner;});
    if(found==surfaces.end()) return;
    if(scene.Entities().IsAlive(owner))
        if(const auto* mesh=scene.Components().MeshRenderers().TryGet(owner);mesh&&mesh->meshAssetId==found->meshAssetId) scene.Components().MeshRenderers().Remove(owner);
    static_cast<void>(scene.Assets().Manager().ReleaseGeneratedAsset(kb::assets::AssetId{found->meshAssetId}));
    surfaces.erase(found);
}
void SceneSurfaceGeometry::Clear(Scene& scene) noexcept {
    auto& surfaces=SceneAccess::State(scene).surfaceGeometries;
    while(!surfaces.empty()) Remove(scene,surfaces.back().owner);
}
} // namespace kb::scene
