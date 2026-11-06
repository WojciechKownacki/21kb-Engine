#include "scene/SceneSurfaceGeometryPublisher.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kb::render {
void PublishSceneSurfaceGeometry(kb::scene::Scene& scene) {
    const auto surfaces=kb::scene::SceneSurfaceGeometry::All(scene);if(surfaces.empty()) return;
    auto& manager=scene.Assets().Manager();
    if(!manager.HasLoaderForType("RenderMesh")&&!manager.RegisterLoader(std::make_unique<RenderMeshAssetLoader>()))
        throw std::runtime_error("surface renderer could not register the mesh loader");
    for(const auto& source:surfaces) {
        if(!scene.Entities().IsAlive(source.owner)) continue;
        const kb::assets::AssetId id{source.meshAssetId};
        const auto* metadata=manager.Registry().Find(id);
        if(metadata&&metadata->contentHash==source.revision) continue;
        const auto previous=manager.AcquirePublishedRuntimeAsset<RenderMeshAssetData>(id);
        auto mesh=std::make_shared<RenderMeshAssetData>();
        const bool sameTopology=previous.IsLoaded()&&previous->vertices.size()==source.vertices.size()&&previous->indices32==source.indices&&
            previous->dynamicVertexUpdateRanges.size()<64;
        if(sameTopology) {
            mesh->dynamicTopologyKey=previous->dynamicTopologyKey;
            mesh->dynamicVertexUpdateRanges=previous->dynamicVertexUpdateRanges;
            mesh->dynamicSectionUpdateIndices=previous->dynamicSectionUpdateIndices;
        } else mesh->dynamicTopologyKey=source.revision;
        mesh->dynamicVertexUpdates=true;
        mesh->indices32=source.indices;
        kb::math::Vec3 low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high{-low.x,-low.y,-low.z};
        mesh->vertices.reserve(source.vertices.size());
        for(const auto& vertex:source.vertices) {
            mesh->vertices.push_back({.x=vertex.position.x,.y=vertex.position.y,.z=vertex.position.z,
                .nx=vertex.normal.x,.ny=vertex.normal.y,.nz=vertex.normal.z,.u=vertex.u,.v=vertex.v});
            low={std::min(low.x,vertex.position.x),std::min(low.y,vertex.position.y),std::min(low.z,vertex.position.z)};
            high={std::max(high.x,vertex.position.x),std::max(high.y,vertex.position.y),std::max(high.z,vertex.position.z)};
        }
        mesh->bounds.center={(low.x+high.x)*0.5F,(low.y+high.y)*0.5F,(low.z+high.z)*0.5F};
        mesh->boundsBox.center=mesh->bounds.center;
        mesh->boundsBox.halfExtents={std::max((high.x-low.x)*0.5F,0.001F),std::max((high.y-low.y)*0.5F,0.001F),std::max((high.z-low.z)*0.5F,0.001F)};
        mesh->bounds.radius=std::sqrt((high.x-low.x)*(high.x-low.x)+(high.y-low.y)*(high.y-low.y)+(high.z-low.z)*(high.z-low.z))*0.5F+0.001F;
        mesh->sections.push_back({.indexCount=static_cast<std::uint32_t>(source.indices.size()),.vertexCount=static_cast<std::uint32_t>(source.vertices.size()),.bounds=mesh->bounds});
        mesh->dynamicVertexUpdateRanges.push_back({.firstVertex=0,.vertexCount=static_cast<std::uint32_t>(source.vertices.size())});
        mesh->dynamicSectionUpdateIndices.push_back(0);
        mesh->RefreshDesc().doubleSided=true;
        const kb::assets::AssetMetadata entry{.id=id,.type="RenderMesh",.name="Deforming surface",
            .virtualPath="/Runtime/Surfaces/"+std::to_string(scene.Id())+"/"+std::to_string(source.owner.Id()),.contentHash=source.revision};
        const bool registered=metadata?manager.Registry().Upsert(entry):manager.RegisterGeneratedAsset(entry);
        if(!registered||!manager.PublishRuntimeAsset(id,mesh)) throw std::runtime_error("could not publish deforming surface mesh: "+manager.LastError());
    }
}
} // namespace kb::render
