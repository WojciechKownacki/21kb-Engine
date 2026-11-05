#include "engine/scene/Rig2DAsset.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
namespace kb::script {
bool RegisterRig2DApi(ScriptRuntimeHost& host) {
    ScriptFunctionDesc f;f.signature.name="Rig2D.Attach";
    f.signature.description="Attach a saved planar weighted skeletal mesh and controller to an entity. Shared Animator playback, seek and blend APIs animate it; components persist with the scene.";
    f.signature.inputs={{"entity",ScriptValueType::Entity,true},{"mesh",ScriptValueType::String,true},{"controller",ScriptValueType::String,true}};
    f.signature.outputs={{"attached",ScriptValueType::Bool,true}};
    f.callback=[](const ScriptFunctionCallContext& c,std::span<const ScriptFunctionArgument> args) {
        const auto arg=[&](std::string_view name)->const ScriptValue& {static const ScriptValue empty;for(const auto& a:args) if(a.name==name) return a.value;return empty;};
        std::string error;
        if(!c.scene||!kb::scene::AttachRig2D(*c.scene,kb::scene::SceneEntity{arg("entity").AsUInt64()},arg("mesh").AsString(),arg("controller").AsString(),error)) return ScriptFunctionCallResult{.executed=false,.outputs={},.errors={error.empty()?"scene is unavailable":std::move(error)}};
        return ScriptFunctionCallResult{.executed=true,.outputs={{"attached",ScriptValue{true}}}};
    };
    return host.RegisterFunction(std::move(f));
}
}
