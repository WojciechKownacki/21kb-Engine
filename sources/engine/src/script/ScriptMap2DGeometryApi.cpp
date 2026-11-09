#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneTilemapRuntime.hpp"
#include "engine/scene/SceneVectorShapes2D.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include <cmath>

namespace kb::script {
namespace {
using Args=std::span<const ScriptFunctionArgument>;
using T=ScriptValueType;
const ScriptValue& Arg(Args args,std::string_view name) {static const ScriptValue empty;for(const auto& a:args) if(a.name==name) return a.value;return empty;}
kb::scene::SceneEntity Owner(Args args) {return kb::scene::SceneEntity{Arg(args,"entity").AsUInt64()};}
ScriptFunctionCallResult Error(std::string error) {return {.executed=false,.outputs={},.errors={std::move(error)}};}
ScriptFunctionCallResult Changed(bool ok,std::string error) {return ok?ScriptFunctionCallResult{.executed=true,.outputs={{"changed",ScriptValue{true}}}}:Error(std::move(error));}
const kb::assets::Map2DAsset* Map(const ScriptFunctionCallContext& c,Args args) {
    const auto* runtime=c.scene?c.scene->Runtime().Tilemaps():nullptr;return runtime?runtime->Map(Owner(args)):nullptr;
}
bool Add(ScriptRuntimeHost& host,std::string name,std::string description,std::vector<ScriptFunctionPin> inputs,std::vector<ScriptFunctionPin> outputs,ScriptFunctionCallback callback) {
    ScriptFunctionDesc f;f.signature.name=std::move(name);f.signature.description=std::move(description);f.signature.inputs=std::move(inputs);f.signature.outputs=std::move(outputs);f.callback=std::move(callback);return host.RegisterFunction(std::move(f));
}
}
bool RegisterMap2DGeometryApi(ScriptRuntimeHost& host) {
    bool ok=Add(host,"Map2D.CellToWorld","Project a valid tile centre through its rectangular/isometric/axial-hex topology and entity transform. Floor selects the map layer.",
        {{"entity",T::Entity,true},{"x",T::Int,true},{"y",T::Int,true},{"floor",T::Int,false}},{{"x",T::Double,true},{"y",T::Double,true},{"z",T::Double,true}},
        [](const ScriptFunctionCallContext& c,Args args) {
            const auto* map=Map(c,args);if(!map) return Error("tilemap is not initialized");
            const int x=Arg(args,"x").AsInt(),y=Arg(args,"y").AsInt(),floor=Arg(args,"floor").AsInt();
            if(x<0||y<0||x>=static_cast<int>(map->width)||y>=static_cast<int>(map->height)||floor<kb::assets::Map2DAsset::MinimumFloor||floor>kb::assets::Map2DAsset::MaximumFloor) return Error("map cell or floor is out of range");
            c.scene->Runtime().SynchronizeTransforms();const auto* transform=c.scene->Transforms().TryGet(Owner(args));if(!transform) return Error("map transform is unavailable");
            const auto p=kb::assets::Map2DCellLocalPoint(*map,x,y,floor);
            const kb::scene::Vec3 scaled{static_cast<float>(p[0])*transform->worldScale.x,static_cast<float>(p[1])*transform->worldScale.y,static_cast<float>(p[2])*transform->worldScale.z};
            if(!std::isfinite(scaled.x)||!std::isfinite(scaled.y)||!std::isfinite(scaled.z)) return Error("map projection exceeds transform range");
            const auto world=c.scene->Transforms().WorldTranslation(Owner(args),*transform)+kb::math::Rotate(transform->worldRotation,scaled);
            return ScriptFunctionCallResult{.executed=true,.outputs={{"x",ScriptValue{world.x}},{"y",ScriptValue{world.y}},{"z",ScriptValue{world.z}}}};
        });
    ok=Add(host,"Map2D.WorldToCell","Pick a tile from a world point using the inverse entity transform and topology. Projects onto the map plane; layer selection is independent. Returns found=false outside the map.",
        {{"entity",T::Entity,true},{"x",T::Double,true},{"y",T::Double,true},{"z",T::Double,true}},{{"found",T::Bool,true},{"x",T::Int,true},{"y",T::Int,true}},
        [](const ScriptFunctionCallContext& c,Args args) {
            const auto* map=Map(c,args);if(!map) return Error("tilemap is not initialized");
            const kb::math::DVec3 p{Arg(args,"x").AsDouble(),Arg(args,"y").AsDouble(),Arg(args,"z").AsDouble()};
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return Error("world point must be finite");
            c.scene->Runtime().SynchronizeTransforms();const auto* t=c.scene->Transforms().TryGet(Owner(args));if(!t||std::abs(t->worldScale.x)<1.0e-8F||std::abs(t->worldScale.y)<1.0e-8F||std::abs(t->worldScale.z)<1.0e-8F) return Error("map transform is not invertible");
            auto local=kb::math::Rotate(kb::math::Conjugate(t->worldRotation),kb::math::ToVec3(p-c.scene->Transforms().WorldTranslation(Owner(args),*t)));
            local={local.x/t->worldScale.x,local.y/t->worldScale.y,local.z/t->worldScale.z};
            const auto size=kb::assets::Map2DProjectedSize(*map);const bool wall=map->orientation==kb::assets::Map2DOrientation::Wall;
            const auto cell=kb::assets::Map2DCellAtPlane(*map,local.x+size[0]*.5,(wall?size[1]:size[1]*.5)-(wall?local.y:local.z));
            return ScriptFunctionCallResult{.executed=true,.outputs={{"found",ScriptValue{cell.has_value()}},{"x",ScriptValue{cell?(*cell)[0]:-1}},{"y",ScriptValue{cell?(*cell)[1]:-1}}}};
        })&&ok;
    ok=Add(host,"Map2D.Neighbor","Read an edge neighbor by index: rectangular/isometric have four, axial hex has six. Count reports directions; found is false for neighbors outside the map.",
        {{"entity",T::Entity,true},{"x",T::Int,true},{"y",T::Int,true},{"index",T::Int,true}},{{"found",T::Bool,true},{"x",T::Int,true},{"y",T::Int,true},{"count",T::Int,true}},
        [](const ScriptFunctionCallContext& c,Args args) {
            const auto* map=Map(c,args);if(!map) return Error("tilemap is not initialized");
            const int x=Arg(args,"x").AsInt(),y=Arg(args,"y").AsInt(),index=Arg(args,"index").AsInt();const auto offsets=kb::assets::Map2DNeighbourOffsets(map->topology);
            if(x<0||y<0||x>=static_cast<int>(map->width)||y>=static_cast<int>(map->height)||index<0||static_cast<std::size_t>(index)>=offsets.size()) return Error("map cell or neighbor index is out of range");
            const int nx=x+offsets[static_cast<std::size_t>(index)][0],ny=y+offsets[static_cast<std::size_t>(index)][1];
            return ScriptFunctionCallResult{.executed=true,.outputs={{"found",ScriptValue{nx>=0&&ny>=0&&nx<static_cast<int>(map->width)&&ny<static_cast<int>(map->height)}},{"x",ScriptValue{nx}},{"y",ScriptValue{ny}},{"count",ScriptValue{static_cast<int>(offsets.size())}}}};
        })&&ok;
    const ScriptFunctionPin entity{"entity",T::Entity,true},changed{"changed",T::Bool,true};
    ok=Add(host,"Vector2D.Load","Install a saved polygon or line asset on a live entity, using the shared dynamic surface renderer. Persist the asset and binding script.",{entity,{"asset",T::String,true}},{changed},
        [](const ScriptFunctionCallContext& c,Args args) {if(!c.scene) return Error("scene is unavailable");std::string error;const bool result=kb::scene::SceneVectorShapes2D::Load(*c.scene,Owner(args),Arg(args,"asset").AsString(),error);return Changed(result,std::move(error));})&&ok;
    ok=Add(host,"Vector2D.SetPoint","Deform one installed outline point in entity-local XY. Invalid polygons are rejected without changing the current surface.",{entity,{"index",T::Int,true},{"x",T::Float,true},{"y",T::Float,true}},{changed},
        [](const ScriptFunctionCallContext& c,Args args) {if(!c.scene) return Error("scene is unavailable");std::string error;const bool result=kb::scene::SceneVectorShapes2D::SetPoint(*c.scene,Owner(args),static_cast<std::uint32_t>(Arg(args,"index").AsInt()),{Arg(args,"x").AsFloat(),Arg(args,"y").AsFloat()},error);return Changed(result,std::move(error));})&&ok;
    ok=Add(host,"Vector2D.Point","Read an installed outline point and point count. The index is zero-based.",{entity,{"index",T::Int,true}},{{"x",T::Float,true},{"y",T::Float,true},{"count",T::Int,true}},
        [](const ScriptFunctionCallContext& c,Args args) {if(!c.scene) return Error("scene is unavailable");const auto shape=kb::scene::SceneVectorShapes2D::Get(*c.scene,Owner(args));const int i=Arg(args,"index").AsInt();if(!shape||i<0||static_cast<std::size_t>(i)>=shape->points.size()) return Error("vector point index is unavailable");const auto p=shape->points[static_cast<std::size_t>(i)];return ScriptFunctionCallResult{.executed=true,.outputs={{"x",ScriptValue{p[0]}},{"y",ScriptValue{p[1]}},{"count",ScriptValue{static_cast<int>(shape->points.size())}}}};})&&ok;
    ok=Add(host,"Vector2D.SetWidth","Change an installed line's stroke width in entity-local units. Rejects polygon shapes.",{entity,{"width",T::Float,true}},{changed},
        [](const ScriptFunctionCallContext& c,Args args) {if(!c.scene) return Error("scene is unavailable");std::string error;const bool result=kb::scene::SceneVectorShapes2D::SetWidth(*c.scene,Owner(args),Arg(args,"width").AsFloat(),error);return Changed(result,std::move(error));})&&ok;
    ok=Add(host,"Vector2D.Clear","Remove the generated surface and restore a mesh replaced when the vector shape was installed.",{entity},{changed},
        [](const ScriptFunctionCallContext& c,Args args) {if(!c.scene) return Error("scene is unavailable");std::string error;const bool result=kb::scene::SceneVectorShapes2D::Clear(*c.scene,Owner(args),error);return Changed(result,std::move(error));})&&ok;
    return ok;
}
}
