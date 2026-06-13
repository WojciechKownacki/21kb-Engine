#include "engine/script/ScriptTimeApi.hpp"

#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <utility>

namespace kb::script {

bool ScriptTimeApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc desc;
    desc.signature.name = "Time.Delta";
    desc.signature.inputs = {};
    desc.signature.outputs = { ScriptFunctionPin{ "delta", ScriptValueType::Float, true } };
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = { ScriptFunctionArgument{ "delta", ScriptValue{ context.deltaSeconds } } },
            .errors = {},
        };
    };
    return host.RegisterFunction(std::move(desc));
}

} // namespace kb::script
