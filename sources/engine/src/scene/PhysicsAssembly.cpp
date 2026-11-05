#include "engine/scene/PhysicsAssembly.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/BuiltInShapes.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace kb::scene {
namespace {
using J = kb::core::JsonValue;
constexpr std::array<std::string_view,5> names{"Ragdoll","Cloth","Vehicle","Fracture","Buoyancy"};
bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Positive(float v) { return std::isfinite(v)&&v>0; }
bool NonNegative(float v) { return std::isfinite(v)&&v>=0; }
J Vector(Vec3 v) { auto a=J::MakeArray();for(float n:{v.x,v.y,v.z}) a.Append(J::MakeNumber(n));return a; }
bool Number(const J& object,std::string_view key,float& out,std::string& error) {
    const auto* v=object.Find(key);if(!v) return true;
    const double n=v->AsNumber(std::numeric_limits<double>::quiet_NaN());
    if(v->GetKind()!=J::Kind::Number||!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max()) {
        error=std::string{key}+" must be a finite number";return false;
    }
    out=static_cast<float>(n);return true;
}
bool Integer(const J& object,std::string_view key,std::uint32_t& out,std::string& error) {
    float n=static_cast<float>(out);if(!Number(object,key,n,error)) return false;
    if(n<0||n>65536||n!=std::floor(n)) {error=std::string{key}+" must be a bounded nonnegative integer";return false;}
    out=static_cast<std::uint32_t>(n);return true;
}
bool ReadVector(const J& object,std::string_view key,Vec3& out,std::string& error) {
    const auto* a=object.Find(key);if(!a) return true;
    if(a->GetKind()!=J::Kind::Array||a->Size()!=3) {error=std::string{key}+" must be [x,y,z]";return false;}
    float* values[]{&out.x,&out.y,&out.z};
    for(std::size_t i=0;i<3;++i) {
        const auto* v=a->At(i);const double n=v->AsNumber(std::numeric_limits<double>::quiet_NaN());
        if(v->GetKind()!=J::Kind::Number||!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max()) {error="invalid vector number";return false;}
        *values[i]=static_cast<float>(n);
    }
    return true;
}
bool Keys(const J& object,std::span<const std::string_view> allowed,std::string& error) {
    if(object.GetKind()!=J::Kind::Object) {error="expected a JSON object";return false;}
    for(std::size_t i=0;i<object.Size();++i) if(std::find(allowed.begin(),allowed.end(),object.MemberName(i))==allowed.end()) {
        error="unknown or inapplicable physics assembly field: "+object.MemberName(i);return false;
    }
    return true;
}
IPhysicsAssemblyBackend* Backend(Scene& scene,std::string& error) {
    auto* backend=SceneAccess::State(scene).physicsAssemblyBackend;
    if(!backend) {error="active physics provider does not support physics assemblies";return nullptr;}
    error.clear();return backend;
}
}

std::string ValidatePhysicsAssembly(const PhysicsAssemblyAsset& a) {
    if(static_cast<unsigned>(a.kind)>=names.size()) return "invalid physics assembly kind";
    switch(a.kind) {
    case PhysicsAssemblyKind::Ragdoll: {
        if(a.bones.empty()||a.bones.size()>64) return "ragdoll needs 1..64 bone shapes";
        std::unordered_set<std::string> used;
        for(std::size_t i=0;i<a.bones.size();++i) {
            const auto& b=a.bones[i];
            if(b.name.empty()||b.name.size()>128||!used.insert(b.name).second||b.parent < -1||b.parent>=static_cast<std::int32_t>(i)||
                (i==0?b.parent!=-1:b.parent<0)||!Finite(b.center)||!Positive(b.radius)||!Positive(b.height)||b.height<2*b.radius||
                (!Positive(b.mass)||b.mass<0.001F||b.mass>1000000.0F)||b.radius>10||b.height>100) return "invalid ragdoll name, parent order, capsule dimensions or mass";
        }
        break;
    }
    case PhysicsAssemblyKind::Cloth:
        if(a.columns<2||a.rows<2||a.columns>32||a.rows>32||!Positive(a.spacing)||a.spacing<0.005F||a.spacing>10||!Positive(a.mass)||a.mass<0.001F||a.mass>1000000.0F||
            !NonNegative(a.compliance)||!NonNegative(a.bendingCompliance)||a.compliance>1||a.bendingCompliance>1) return "invalid cloth grid, mass or compliance";
        break;
    case PhysicsAssemblyKind::Vehicle:
        if(!Finite(a.size)||!Positive(a.size.x)||!Positive(a.size.y)||!Positive(a.size.z)||!Positive(a.mass)||
            !Positive(a.wheelRadius)||!Positive(a.suspensionLength)||a.suspensionLength<0.1F||a.suspensionLength>5||!Positive(a.engineTorque)||a.engineTorque>10000||
            a.mass<1||a.mass>1000000.0F||a.size.x>100||a.size.y>100||a.size.z>100||a.wheelRadius>10) return "invalid vehicle dimensions, suspension, torque or mass";
        break;
    case PhysicsAssemblyKind::Fracture:
        if(!Finite(a.size)||!Positive(a.size.x)||!Positive(a.size.y)||!Positive(a.size.z)||!Positive(a.mass)||
            a.mass<0.001F||a.mass>1000000.0F||a.size.x>100||a.size.y>100||a.size.z>100||
            a.cellsX<1||a.cellsY<1||a.cellsZ<1||a.cellsX>8||a.cellsY>8||a.cellsZ>8||
            a.cellsX*a.cellsY*a.cellsZ<2||a.cellsX*a.cellsY*a.cellsZ>64) return "fracture needs positive dimensions/mass and 2..64 box cells";
        break;
    case PhysicsAssemblyKind::Buoyancy:
        if(!std::isfinite(a.waterLevel)||!Positive(a.buoyancy)||!NonNegative(a.linearDrag)||!NonNegative(a.angularDrag)||!Finite(a.fluidVelocity)||std::abs(a.waterLevel)>1000000||a.buoyancy>100||a.linearDrag>100||a.angularDrag>100)
            return "invalid water plane, buoyancy or drag";
        break;
    }
    return {};
}

bool DecodePhysicsAssembly(std::string_view source,PhysicsAssemblyAsset& out,std::string& error) {
    error.clear();
    J root;if(!J::Parse(source,root,error)||root.GetKind()!=J::Kind::Object) {if(error.empty()) error="physics assembly must be a JSON object";return false;}
    const auto* schema=root.Find("schema"),*kind=root.Find("kind");
    if(!schema||schema->AsString()!="21kb.physics-assembly/v1"||!kind||kind->GetKind()!=J::Kind::String) {error="invalid physics assembly schema or kind";return false;}
    const auto found=std::find(names.begin(),names.end(),kind->AsString());
    if(found==names.end()) {error="unknown physics assembly kind";return false;}
    PhysicsAssemblyAsset a;a.kind=static_cast<PhysicsAssemblyKind>(found-names.begin());
    std::vector<std::string_view> keys{"schema","kind"};
    const auto fields=[&](std::initializer_list<std::string_view> list) {keys.insert(keys.end(),list.begin(),list.end());};
    switch(a.kind) {
    case PhysicsAssemblyKind::Ragdoll: {
        fields({"bones"});const auto* bones=root.Find("bones");
        if(!bones||bones->GetKind()!=J::Kind::Array||bones->Size()>64) {error="ragdoll needs a bones array";return false;}
        constexpr std::array<std::string_view,6> boneKeys{"name","parent","center","radius","height","mass"};
        for(std::size_t i=0;i<bones->Size();++i) {
            const auto& b=*bones->At(i);PhysicsBoneShape bone;
            if(!Keys(b,boneKeys,error)) return false;
            const auto* name=b.Find("name"),*parent=b.Find("parent");
            if(!name||name->GetKind()!=J::Kind::String||!parent||parent->GetKind()!=J::Kind::Number||
                parent->AsNumber() < -1||parent->AsNumber()>63||parent->AsNumber()!=std::floor(parent->AsNumber())) {error="bone needs name and integer parent";return false;}
            bone.name=name->AsString();bone.parent=static_cast<std::int32_t>(parent->AsNumber());
            if(!ReadVector(b,"center",bone.center,error)||!Number(b,"radius",bone.radius,error)||!Number(b,"height",bone.height,error)||!Number(b,"mass",bone.mass,error)) return false;
            a.bones.push_back(std::move(bone));
        }
        break;
    }
    case PhysicsAssemblyKind::Cloth:
        fields({"columns","rows","spacing","mass","compliance","bendingCompliance","pinTopEdge"});
        if(!Integer(root,"columns",a.columns,error)||!Integer(root,"rows",a.rows,error)||!Number(root,"spacing",a.spacing,error)||
            !Number(root,"mass",a.mass,error)||!Number(root,"compliance",a.compliance,error)||!Number(root,"bendingCompliance",a.bendingCompliance,error)) return false;
        if(const auto* pin=root.Find("pinTopEdge")) {if(pin->GetKind()!=J::Kind::Bool) {error="pinTopEdge must be boolean";return false;}a.pinTopEdge=pin->AsBool();}
        break;
    case PhysicsAssemblyKind::Vehicle:
        fields({"size","mass","wheelRadius","suspensionLength","engineTorque"});
        if(!ReadVector(root,"size",a.size,error)||!Number(root,"mass",a.mass,error)||!Number(root,"wheelRadius",a.wheelRadius,error)||
            !Number(root,"suspensionLength",a.suspensionLength,error)||!Number(root,"engineTorque",a.engineTorque,error)) return false;
        break;
    case PhysicsAssemblyKind::Fracture:
        fields({"size","mass","cellsX","cellsY","cellsZ"});
        if(!ReadVector(root,"size",a.size,error)||!Number(root,"mass",a.mass,error)||!Integer(root,"cellsX",a.cellsX,error)||
            !Integer(root,"cellsY",a.cellsY,error)||!Integer(root,"cellsZ",a.cellsZ,error)) return false;
        break;
    case PhysicsAssemblyKind::Buoyancy:
        fields({"waterLevel","buoyancy","linearDrag","angularDrag","fluidVelocity"});
        if(!Number(root,"waterLevel",a.waterLevel,error)||!Number(root,"buoyancy",a.buoyancy,error)||!Number(root,"linearDrag",a.linearDrag,error)||
            !Number(root,"angularDrag",a.angularDrag,error)||!ReadVector(root,"fluidVelocity",a.fluidVelocity,error)) return false;
        break;
    }
    if(!Keys(root,keys,error)) return false;
    error=ValidatePhysicsAssembly(a);if(!error.empty()) return false;out=std::move(a);return true;
}

std::string EncodePhysicsAssembly(const PhysicsAssemblyAsset& a) {
    if(!ValidatePhysicsAssembly(a).empty()) return {};
    auto root=J::MakeObject();root.Set("schema",J::MakeString("21kb.physics-assembly/v1"));root.Set("kind",J::MakeString(std::string{names[static_cast<unsigned>(a.kind)]}));
    const auto number=[&](const char* key,double value) {root.Set(key,J::MakeNumber(value));};
    switch(a.kind) {
    case PhysicsAssemblyKind::Ragdoll: {
        auto bones=J::MakeArray();for(const auto& b:a.bones) {auto bone=J::MakeObject();bone.Set("name",J::MakeString(b.name));bone.Set("parent",J::MakeNumber(b.parent));
            bone.Set("center",Vector(b.center));bone.Set("radius",J::MakeNumber(b.radius));bone.Set("height",J::MakeNumber(b.height));bone.Set("mass",J::MakeNumber(b.mass));bones.Append(std::move(bone));}
        root.Set("bones",std::move(bones));break;
    }
    case PhysicsAssemblyKind::Cloth:
        number("columns",a.columns);number("rows",a.rows);number("spacing",a.spacing);number("mass",a.mass);number("compliance",a.compliance);number("bendingCompliance",a.bendingCompliance);root.Set("pinTopEdge",J::MakeBool(a.pinTopEdge));break;
    case PhysicsAssemblyKind::Vehicle:
        root.Set("size",Vector(a.size));number("mass",a.mass);number("wheelRadius",a.wheelRadius);number("suspensionLength",a.suspensionLength);number("engineTorque",a.engineTorque);break;
    case PhysicsAssemblyKind::Fracture:
        root.Set("size",Vector(a.size));number("mass",a.mass);number("cellsX",a.cellsX);number("cellsY",a.cellsY);number("cellsZ",a.cellsZ);break;
    case PhysicsAssemblyKind::Buoyancy:
        number("waterLevel",a.waterLevel);number("buoyancy",a.buoyancy);number("linearDrag",a.linearDrag);number("angularDrag",a.angularDrag);root.Set("fluidVelocity",Vector(a.fluidVelocity));break;
    }
    return root.Dump()+"\n";
}

bool SavePhysicsAssembly(const std::filesystem::path& path,const PhysicsAssemblyAsset& asset,std::string& error) {
    error=ValidatePhysicsAssembly(asset);if(!error.empty()) return false;
    const auto text=EncodePhysicsAssembly(asset);std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(text.data(),static_cast<std::streamsize>(text.size()));output.close();
    if(!output) {error="could not save physics assembly";return false;}return true;
}

kb::assets::AssetLoadResult PhysicsAssemblyAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> bytes;std::string error;PhysicsAssemblyAsset asset;
    if(!request.ReadSourceBytes(bytes,error)||!DecodePhysicsAssembly(std::string_view{reinterpret_cast<const char*>(bytes.data()),bytes.size()},asset,error)) return {.asset={},.error=std::move(error)};
    return {.asset=std::make_shared<PhysicsAssemblyAsset>(std::move(asset)),.error={}};
}

std::vector<kb::assets::AssetId> PhysicsAssemblyAssetLoader::DiscoverDependencies(const kb::assets::AssetMetadata& metadata,const kb::assets::AssetRegistry&) const {
    const kb::assets::AssetLoadRequest request{.metadata=metadata,.resolvedPath=metadata.physicalPath};
    std::vector<std::uint8_t> bytes;std::string error;PhysicsAssemblyAsset asset;
    if(!request.ReadSourceBytes(bytes,error)||!DecodePhysicsAssembly(std::string_view{reinterpret_cast<const char*>(bytes.data()),bytes.size()},asset,error)) return {};
    if(asset.kind==PhysicsAssemblyKind::Vehicle) return {kb::assets::BuiltInShapeId(kb::assets::BuiltInShape::Cube),kb::assets::BuiltInShapeId(kb::assets::BuiltInShape::Cylinder)};
    if(asset.kind==PhysicsAssemblyKind::Fracture) return {kb::assets::BuiltInShapeId(kb::assets::BuiltInShape::Cube)};
    return {};
}

std::optional<std::string> PhysicsAssemblyAssetLoader::ValidateDependencies(const kb::assets::AssetMetadata& metadata,const kb::assets::AssetRegistry& registry) const {
    const kb::assets::AssetLoadRequest request{.metadata=metadata,.resolvedPath=metadata.physicalPath};
    return ValidateRuntimeDependencies(request,registry);
}

std::optional<std::string> PhysicsAssemblyAssetLoader::ValidateRuntimeDependencies(const kb::assets::AssetLoadRequest& request,const kb::assets::AssetRegistry&) const {
    std::vector<std::uint8_t> bytes;std::string error;PhysicsAssemblyAsset asset;
    if(!request.ReadSourceBytes(bytes,error)||!DecodePhysicsAssembly(std::string_view{reinterpret_cast<const char*>(bytes.data()),bytes.size()},asset,error)) return error;
    return std::nullopt;
}

void PhysicsAssemblies::RegisterBackend(Scene& scene,IPhysicsAssemblyBackend& backend) {SceneAccess::State(scene).physicsAssemblyBackend=&backend;}
void PhysicsAssemblies::UnregisterBackend(Scene& scene,IPhysicsAssemblyBackend& backend) noexcept {
    auto& pointer=SceneAccess::State(scene).physicsAssemblyBackend;if(pointer==&backend) pointer=nullptr;
}
PhysicsAssemblyResult PhysicsAssemblies::Create(Scene& scene,SceneEntity owner,const PhysicsAssemblyAsset& asset) {
    auto error=ValidatePhysicsAssembly(asset);if(!error.empty()) return {0,std::move(error)};
    if(!scene.Entities().IsAlive(owner)) return {0,"physics assembly owner is not alive"};
    auto* backend=Backend(scene,error);return backend?backend->Create(owner,asset):PhysicsAssemblyResult{0,std::move(error)};
}
PhysicsAssemblyResult PhysicsAssemblies::Load(Scene& scene,SceneEntity owner,std::string_view assetPath) {
    auto& manager=scene.Assets().Manager();const auto* metadata=manager.Registry().FindByPath(std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(assetPath.data()),assetPath.size()}});
    if(!metadata||metadata->type!="PhysicsAssembly") return {0,"physics assembly asset was not found"};
    const auto asset=manager.Load<PhysicsAssemblyAsset>(metadata->id);if(!asset.IsLoaded()) return {0,manager.LastError()};return Create(scene,owner,*asset.Shared());
}
bool PhysicsAssemblies::Destroy(Scene& scene,std::uint64_t handle,std::string& error) {auto* b=Backend(scene,error);return b&&b->Destroy(handle,error);}
bool PhysicsAssemblies::State(Scene& scene,std::uint64_t handle,PhysicsAssemblyState& out,std::string& error) {auto* b=Backend(scene,error);return b&&b->State(handle,out,error);}
bool PhysicsAssemblies::Drive(Scene& scene,std::uint64_t handle,float throttle,float steering,float brake,std::string& error) {
    if(!std::isfinite(throttle)||!std::isfinite(steering)||!std::isfinite(brake)||std::abs(throttle)>1||std::abs(steering)>1||brake<0||brake>1) {error="vehicle inputs require throttle/steering in [-1,1], brake in [0,1]";return false;}
    auto* b=Backend(scene,error);return b&&b->Drive(handle,throttle,steering,brake,error);
}
bool PhysicsAssemblies::SimulateRagdoll(Scene& scene,std::uint64_t handle,bool enabled,std::string& error) {auto* b=Backend(scene,error);return b&&b->SimulateRagdoll(handle,enabled,error);}
bool PhysicsAssemblies::Fracture(Scene& scene,std::uint64_t handle,Vec3 impulse,std::string& error) {
    if(!Finite(impulse)) {error="fracture impulse must be finite";return false;}auto* b=Backend(scene,error);return b&&b->Fracture(handle,impulse,error);
}
bool PhysicsAssemblies::SetWaterLevel(Scene& scene,std::uint64_t handle,float level,std::string& error) {
    if(!std::isfinite(level)) {error="water level must be finite";return false;}auto* b=Backend(scene,error);return b&&b->SetWaterLevel(handle,level,error);
}
} // namespace kb::scene
