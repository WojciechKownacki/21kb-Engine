#include "engine/script/ScriptLocalizationApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneLocalization.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* Arg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) if (argument.name == name) return &argument.value;
    return nullptr;
}

ScriptFunctionCallResult NoScene() {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { "Localization API requires an active scene" } };
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs,
              std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc{};
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptLocalizationApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Localization.SetCatalog", { { "catalog", ScriptValueType::Hash, true } }, { { "set", ScriptValueType::Bool, true } },
        [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
            if (context.scene == nullptr) return NoScene();
            const bool set = context.scene->Localization().SetCatalog(Arg(arguments, "catalog")->AsUInt64());
            return ScriptFunctionCallResult{ .executed = true, .outputs = { { "set", ScriptValue{ set } } } };
        }) && ok;
    ok = RegisterFunction(host, "Localization.SetLanguage", { { "language", ScriptValueType::String, true } }, { { "set", ScriptValueType::Bool, true } },
        [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
            if (context.scene == nullptr) return NoScene();
            const bool set = context.scene->Localization().SetLanguage(Arg(arguments, "language")->AsString());
            return ScriptFunctionCallResult{ .executed = true, .outputs = { { "set", ScriptValue{ set } } } };
        }) && ok;
    ok = RegisterFunction(host, "Localization.Language", {}, { { "language", ScriptValueType::String, true } },
        [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
            if (context.scene == nullptr) return NoScene();
            return ScriptFunctionCallResult{ .executed = true, .outputs = { { "language", ScriptValue{ context.scene->Localization().Language() } } } };
        }) && ok;
    ok = RegisterFunction(host, "Localization.Translate", { { "key", ScriptValueType::String, true } }, { { "text", ScriptValueType::String, true } },
        [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
            if (context.scene == nullptr) return NoScene();
            return ScriptFunctionCallResult{ .executed = true, .outputs = { { "text", ScriptValue{ context.scene->Localization().Translate(Arg(arguments, "key")->AsString()) } } } };
        }) && ok;
    ok = RegisterFunction(host, "Localization.FormatPlural", { { "key", ScriptValueType::String, true }, { "count", ScriptValueType::Int, true } }, { { "text", ScriptValueType::String, true } },
        [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
            if (context.scene == nullptr) return NoScene();
            return ScriptFunctionCallResult{ .executed = true, .outputs = { { "text", ScriptValue{ context.scene->Localization().FormatPlural(Arg(arguments, "key")->AsString(), Arg(arguments, "count")->AsInt()) } } } };
        }) && ok;
    return ok;
}

} // namespace kb::script
