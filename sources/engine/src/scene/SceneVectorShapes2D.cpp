#include "engine/scene/SceneVectorShapes2D.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/math/Polygon2D.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include <algorithm>
#include <cmath>

namespace kb::scene {
namespace {
using Point=std::array<float,2>;
void Prune(Scene& scene) {
    auto& records=SceneAccess::State(scene).vectorShapes2D;
    std::erase_if(records,[&](const auto& record){
        if(scene.Entities().IsAlive(record.owner)) return false;
        SceneSurfaceGeometry::Remove(scene,record.owner);return true;
    });
}
SurfaceGeometryVertex Vertex(Point point,float u,float v) {return {.position={point[0],point[1],0},.normal={0,0,-1},.u=u,.v=v};}
bool Build(const VectorShape2DAsset& shape,std::vector<SurfaceGeometryVertex>& vertices,std::vector<std::uint32_t>& indices,std::string& error) {
    if(shape.kind==VectorShape2DKind::Polygon) {
        kb::math::Polygon2DTriangulation polygon;if(!kb::math::TriangulatePolygon2D(shape.points,polygon,error)) return false;
        Point low=polygon.points.front(),high=low;for(const auto p:polygon.points) for(std::size_t axis=0;axis<2;++axis) {low[axis]=std::min(low[axis],p[axis]);high[axis]=std::max(high[axis],p[axis]);}
        for(const auto p:polygon.points) vertices.push_back(Vertex(p,(p[0]-low[0])/std::max(high[0]-low[0],0.00001F),(p[1]-low[1])/std::max(high[1]-low[1],0.00001F)));
        indices=std::move(polygon.indices);return true;
    }
    const std::size_t pointCount=shape.points.size();
    if(pointCount<2U) {error="vector shape needs at least two points";return false;}
    const std::size_t count=shape.closed?pointCount:pointCount-1U;
    std::vector<Point> normals;normals.reserve(count);
    std::vector<float> distances{0};float total=0;
    for(std::size_t i=0;i<count;++i) {
        const auto a=shape.points[i],b=shape.points[(i+1)%shape.points.size()];const float dx=b[0]-a[0],dy=b[1]-a[1],length=std::sqrt(dx*dx+dy*dy);
        normals.push_back({-dy/length*shape.width*.5F,dx/length*shape.width*.5F});total+=length;distances.push_back(total);
    }
    for(std::size_t i=0;i<count;++i) {
        const auto a=shape.points[i],b=shape.points[(i+1)%shape.points.size()],n=normals[i];const auto base=static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(Vertex({a[0]+n[0],a[1]+n[1]},distances[i]/total,0));vertices.push_back(Vertex({a[0]-n[0],a[1]-n[1]},distances[i]/total,1));
        vertices.push_back(Vertex({b[0]-n[0],b[1]-n[1]},distances[i+1]/total,1));vertices.push_back(Vertex({b[0]+n[0],b[1]+n[1]},distances[i+1]/total,0));
        indices.insert(indices.end(),{base,base+2,base+1,base,base+3,base+2});
    }
    // Bevel joins fill the outside of each turn. Segment rectangles already
    // cover the inside. End caps are butt caps; crossing lines draw overlapping triangles.
    const std::size_t begin=shape.closed?0U:1U,end=shape.closed?shape.points.size():shape.points.size()-1;
    for(std::size_t i=begin;i<end;++i) {
        const auto p=shape.points[i],before=normals[(i+count-1)%count],after=normals[i%count];
        const float turn=before[0]*after[1]-before[1]*after[0];if(std::abs(turn)<1.0e-12F) continue;
        const float side=turn>0?-1.0F:1.0F;const auto base=static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(Vertex(p,distances[i]/total,.5F));vertices.push_back(Vertex({p[0]+before[0]*side,p[1]+before[1]*side},distances[i]/total,0));
        vertices.push_back(Vertex({p[0]+after[0]*side,p[1]+after[1]*side},distances[i]/total,1));if(turn>0) indices.insert(indices.end(),{base,base+2,base+1});else indices.insert(indices.end(),{base,base+1,base+2});
    }
    return true;
}
}
bool SceneVectorShapes2D::Set(Scene& scene,SceneEntity owner,const VectorShape2DAsset& shape,std::string& error) {
    error=ValidateVectorShape2D(shape);if(!error.empty()) return false;
    if(!scene.Entities().IsAlive(owner)) {error="vector shape owner is not alive";return false;}Prune(scene);
    auto& records=SceneAccess::State(scene).vectorShapes2D;
    auto record=std::find_if(records.begin(),records.end(),[&](const auto& r){return r.owner==owner;});
    const bool fresh=record==records.end();
    if(fresh) {
        if(records.size()>=256) {error="vector shape budget exceeded";return false;}
        const auto surfaces=SceneSurfaceGeometry::All(scene);
        if(std::any_of(surfaces.begin(),surfaces.end(),[&](const auto& surface){return surface.owner==owner;})) {error="owner already has a dynamic surface";return false;}
    }
    std::uint64_t material=0;
    if(!shape.material.empty()) {
        const auto* metadata=scene.Assets().Manager().Registry().FindByPath(kb::assets::Utf8Path(shape.material));
        if(!metadata||metadata->type!="RenderMaterial"||!metadata->runtimeLoadable) {error="vector shape material is not available";return false;}material=metadata->id.value;
    }
    std::optional<MeshRendererComponent> previous;
    if(fresh) if(const auto* mesh=scene.Components().MeshRenderers().TryGet(owner)) previous=*mesh;
    std::vector<SurfaceGeometryVertex> vertices;std::vector<std::uint32_t> indices;
    if(!Build(shape,vertices,indices,error)||!SceneSurfaceGeometry::Publish(scene,owner,vertices,indices,error)) return false;
    auto mesh=*scene.Components().MeshRenderers().TryGet(owner);mesh.materialAssetId=material;mesh.materialInstanceHandle=0;mesh.materialSlotOverrideCount=0;mesh.materialSlotAssetIds.fill(0);
    mesh.castsShadow=false;mesh.receivesShadow=false;scene.Components().MeshRenderers().Set(owner,mesh);
    if(fresh) records.push_back({owner,shape,previous});else record->shape=shape;
    error.clear();return true;
}
bool SceneVectorShapes2D::Load(Scene& scene,SceneEntity owner,std::string_view path,std::string& error) {
    const auto* metadata=scene.Assets().Manager().Registry().FindByPath(kb::assets::Utf8Path(path));
    if(!metadata||metadata->type!="VectorShape2D") {error="vector shape asset was not found";return false;}
    const auto shape=scene.Assets().Manager().Load<VectorShape2DAsset>(metadata->id);
    if(!shape.IsLoaded()) {error=scene.Assets().Manager().LastError();return false;}return Set(scene,owner,*shape.Shared(),error);
}
std::optional<VectorShape2DAsset> SceneVectorShapes2D::Get(const Scene& scene,SceneEntity owner) {
    if(!scene.Entities().IsAlive(owner)) return {};
    const auto& records=SceneAccess::State(scene).vectorShapes2D;const auto record=std::find_if(records.begin(),records.end(),[&](const auto& r){return r.owner==owner;});
    return record==records.end()?std::nullopt:std::optional{record->shape};
}
bool SceneVectorShapes2D::SetPoint(Scene& scene,SceneEntity owner,std::uint32_t index,Point point,std::string& error) {
    auto shape=Get(scene,owner);if(!shape||index>=shape->points.size()) {error="vector point index is out of range or owner has no shape";return false;}
    shape->points[index]=point;return Set(scene,owner,*shape,error);
}
bool SceneVectorShapes2D::SetWidth(Scene& scene,SceneEntity owner,float width,std::string& error) {
    auto shape=Get(scene,owner);if(!shape||shape->kind!=VectorShape2DKind::Line) {error="width requires an installed line shape";return false;}
    shape->width=width;return Set(scene,owner,*shape,error);
}
bool SceneVectorShapes2D::Clear(Scene& scene,SceneEntity owner,std::string& error) {
    Prune(scene);auto& records=SceneAccess::State(scene).vectorShapes2D;const auto record=std::find_if(records.begin(),records.end(),[&](const auto& r){return r.owner==owner;});
    if(record==records.end()) {error="owner has no vector shape";return false;}
    SceneSurfaceGeometry::Remove(scene,owner);if(record->previousMesh&&scene.Entities().IsAlive(owner)) scene.Components().MeshRenderers().Set(owner,*record->previousMesh);
    records.erase(record);error.clear();return true;
}
}
