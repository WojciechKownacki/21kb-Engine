#include "engine/scene/PhysicsAssembly.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include <cmath>

namespace kb::script {
namespace {
const ScriptValue* Arg(std::span<const ScriptFunctionArgument> args,std::string_view name) {
    for(const auto& value:args) if(value.name==name) return &value.value;return nullptr;
}
std::uint64_t Handle(std::span<const ScriptFunctionArgument> args) {const auto* v=Arg(args,"handle");return v?v->AsUInt64():0;}
float Number(std::span<const ScriptFunctionArgument> args,std::string_view name,float fallback=0) {const auto* v=Arg(args,name);return v?v->AsFloat():fallback;}
ScriptFunctionCallResult Error(std::string error) {return {.executed=false,.outputs={},.errors={std::move(error)}};}
ScriptFunctionCallResult Changed(bool success,std::string error) {
    return success?ScriptFunctionCallResult{.executed=true,.outputs={{"changed",ScriptValue{true}}}}:Error(std::move(error));
}
bool Register(ScriptRuntimeHost& host,std::string name,std::vector<ScriptFunctionPin> inputs,std::vector<ScriptFunctionPin> outputs,ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;desc.signature.name=std::move(name);desc.signature.inputs=std::move(inputs);desc.signature.outputs=std::move(outputs);desc.callback=std::move(callback);return host.RegisterFunction(std::move(desc));
}
}

bool RegisterPhysicsAssemblyApi(ScriptRuntimeHost& host) {
    const ScriptFunctionPin handle{"handle",ScriptValueType::Hash,true},changed{"changed",ScriptValueType::Bool,true};
    bool ok=Register(host,"Physics.AssemblyLoad",{{"entity",ScriptValueType::Entity,true},{"asset",ScriptValueType::String,true}},{handle},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");
            const auto* entity=Arg(args,"entity"),*asset=Arg(args,"asset");
            const auto result=kb::scene::PhysicsAssemblies::Load(*c.scene,kb::scene::SceneEntity{entity?entity->AsUInt64():0},asset?asset->AsString():"");
            return result.Succeeded()?ScriptFunctionCallResult{.executed=true,.outputs={{"handle",ScriptValue{result.handle,ScriptValueType::Hash}}}}:Error(result.error);
        });
    ok=Register(host,"Physics.AssemblyDestroy",{handle},{changed},[](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
        if(!c.scene) return Error("scene context is unavailable");std::string error;const bool success=kb::scene::PhysicsAssemblies::Destroy(*c.scene,Handle(args),error);return Changed(success,std::move(error));
    })&&ok;
    ok=Register(host,"Physics.AssemblyDrive",{handle,{"throttle",ScriptValueType::Float,true},{"steering",ScriptValueType::Float,true},{"brake",ScriptValueType::Float,true}},{changed},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;
            const bool success=kb::scene::PhysicsAssemblies::Drive(*c.scene,Handle(args),Number(args,"throttle"),Number(args,"steering"),Number(args,"brake"),error);return Changed(success,std::move(error));
        })&&ok;
    ok=Register(host,"Physics.AssemblySimulateRagdoll",{handle,{"enabled",ScriptValueType::Bool,true}},{changed},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;const auto* enabled=Arg(args,"enabled");
            const bool success=kb::scene::PhysicsAssemblies::SimulateRagdoll(*c.scene,Handle(args),enabled&&enabled->AsBool(),error);return Changed(success,std::move(error));
        })&&ok;
    ok=Register(host,"Physics.AssemblyFracture",{handle,{"x",ScriptValueType::Float,true},{"y",ScriptValueType::Float,true},{"z",ScriptValueType::Float,true}},{changed},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;
            const bool success=kb::scene::PhysicsAssemblies::Fracture(*c.scene,Handle(args),{Number(args,"x"),Number(args,"y"),Number(args,"z")},error);return Changed(success,std::move(error));
        })&&ok;
    ok=Register(host,"Physics.AssemblyWaterLevel",{handle,{"level",ScriptValueType::Float,true}},{changed},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;
            const bool success=kb::scene::PhysicsAssemblies::SetWaterLevel(*c.scene,Handle(args),Number(args,"level"),error);return Changed(success,std::move(error));
        })&&ok;
    ok=Register(host,"Physics.AssemblyMetric",{handle,{"field",ScriptValueType::String,true}},{{"value",ScriptValueType::Float,true}},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;kb::scene::PhysicsAssemblyState state;
            if(!kb::scene::PhysicsAssemblies::State(*c.scene,Handle(args),state,error)) return Error(std::move(error));
            const auto* arg=Arg(args,"field");const auto name=arg?arg->AsString():"";float value=0;
            if(name=="ready") value=state.ready?1.0F:0.0F;else if(name=="simulated") value=state.simulated?1.0F:0.0F;
            else if(name=="fractured") value=state.fractured?1.0F:0.0F;else if(name=="submerged") value=state.submerged?1.0F:0.0F;
            else if(name=="speed") value=state.speed;else if(name=="contacts") value=static_cast<float>(state.wheelContacts);
            else if(name=="entityCount") value=static_cast<float>(state.entities.size());else if(name=="vertexCount") value=static_cast<float>(state.vertices.size());
            else return Error("unknown assembly metric; use ready, simulated, fractured, submerged, speed, contacts, entityCount or vertexCount");
            return ScriptFunctionCallResult{.executed=true,.outputs={{"value",ScriptValue{value}}}};
        })&&ok;
    ok=Register(host,"Physics.AssemblyVertex",{handle,{"index",ScriptValueType::Int,true}},{{"x",ScriptValueType::Float,true},{"y",ScriptValueType::Float,true},{"z",ScriptValueType::Float,true}},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;kb::scene::PhysicsAssemblyState state;
            if(!kb::scene::PhysicsAssemblies::State(*c.scene,Handle(args),state,error)) return Error(std::move(error));
            const auto* arg=Arg(args,"index");const int index=arg?arg->AsInt():-1;
            if(index<0||static_cast<std::size_t>(index)>=state.vertices.size()) return Error("assembly vertex index is out of range");
            const auto p=state.vertices[static_cast<std::size_t>(index)];return ScriptFunctionCallResult{.executed=true,.outputs={{"x",ScriptValue{p.x}},{"y",ScriptValue{p.y}},{"z",ScriptValue{p.z}}}};
        })&&ok;
    ok=Register(host,"Physics.AssemblyEntity",{handle,{"index",ScriptValueType::Int,true}},{{"entity",ScriptValueType::Entity,true}},
        [](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
            if(!c.scene) return Error("scene context is unavailable");std::string error;kb::scene::PhysicsAssemblyState state;
            if(!kb::scene::PhysicsAssemblies::State(*c.scene,Handle(args),state,error)) return Error(std::move(error));
            const auto* arg=Arg(args,"index");const int index=arg?arg->AsInt():-1;
            if(index<0||static_cast<std::size_t>(index)>=state.entities.size()) return Error("assembly entity index is out of range");
            return ScriptFunctionCallResult{.executed=true,.outputs={{"entity",ScriptValue{state.entities[static_cast<std::size_t>(index)].Id(),ScriptValueType::Entity}}}};
        })&&ok;
    return ok;
}
} // namespace kb::script
