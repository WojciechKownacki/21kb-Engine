#include "engine/scene/CsgAsset.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
namespace kb::script {
bool RegisterCsgApi(ScriptRuntimeHost& host) {
    ScriptFunctionDesc f;f.signature.name="CSG.Attach";
    f.signature.description="Attach a compiled ordinary mesh and collision asset as a persistent static blockout body.";
    f.signature.inputs={{"entity",ScriptValueType::Entity,true},{"mesh",ScriptValueType::String,true},{"collision",ScriptValueType::String,true}};
    f.signature.outputs={{"attached",ScriptValueType::Bool,true}};
    f.callback=[](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
        const auto arg=[&](std::string_view name)->const ScriptValue& {static const ScriptValue empty;for(const auto& a:args) if(a.name==name) return a.value;return empty;};
        std::string error;
        if(!c.scene||!kb::scene::AttachCompiledCsg(*c.scene,kb::scene::SceneEntity{arg("entity").AsUInt64()},arg("mesh").AsString(),arg("collision").AsString(),error)) return ScriptFunctionCallResult{.executed=false,.outputs={},.errors={error.empty()?"scene is unavailable":std::move(error)}};
        return ScriptFunctionCallResult{.executed=true,.outputs={{"attached",ScriptValue{true}}}};
    };
    return host.RegisterFunction(std::move(f));
}
}
