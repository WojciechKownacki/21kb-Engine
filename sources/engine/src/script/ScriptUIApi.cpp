#include "engine/script/ScriptUIApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneUIDocuments.hpp"
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

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

kb::scene::SceneEntity Target(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* explicitEntity = Arg(arguments, "entity");
    return explicitEntity == nullptr ? context.caller : kb::scene::SceneEntity{ explicitEntity->AsUInt64() };
}

ScriptFunctionCallResult Applied(bool applied, std::string failure) {
    return applied ? ScriptFunctionCallResult{ .executed = true, .outputs = { { "applied", ScriptValue{ true } } } }
                   : Error(std::move(failure));
}

ScriptFunctionCallResult Create(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.Create requires an active scene");
    const ScriptValue* styleClass = Arg(arguments, "styleClass");
    const ScriptValue* visible = Arg(arguments, "visible");
    const auto element = context.scene->UIDocuments().QueueCreate(Target(context, arguments), kb::scene::UIRuntimeElementDesc{
        .parentId = Arg(arguments, "parent")->AsUInt64(),
        .name = Arg(arguments, "name")->AsString(),
        .styleClass = styleClass == nullptr ? std::string{} : styleClass->AsString(),
        .visible = visible == nullptr || visible->AsBool(),
    });
    if (!element.has_value()) return Error("UI.Create requires a live UI document, a live parent, a non-empty name, and queue capacity");
    return ScriptFunctionCallResult{ .executed = true, .outputs = { { "element", ScriptValue{ *element, ScriptValueType::Hash } } } };
}

ScriptFunctionCallResult Destroy(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("UI.Destroy requires an active scene");
    return Applied(context.scene->UIDocuments().QueueDestroy(Target(context, arguments), Arg(arguments, "element")->AsUInt64()),
        "UI.Destroy requires a live non-root runtime element that is not already queued for destruction");
}

ScriptFunctionCallResult SetVisible(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments, bool visible) {
    if (context.scene == nullptr) return Error("UI visibility API requires an active scene");
    const bool queued = visible
        ? context.scene->UIDocuments().QueueShow(Target(context, arguments), Arg(arguments, "element")->AsUInt64())
        : context.scene->UIDocuments().QueueHide(Target(context, arguments), Arg(arguments, "element")->AsUInt64());
    return Applied(queued, "UI visibility command requires a live element that is not queued for destruction");
}

ScriptFunctionCallResult Show(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) { return SetVisible(context, arguments, true); }
ScriptFunctionCallResult Hide(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) { return SetVisible(context, arguments, false); }

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc function{};
    function.signature.name = std::move(name);
    function.signature.inputs = std::move(inputs);
    function.signature.outputs = std::move(outputs);
    function.callback = std::move(callback);
    return host.RegisterFunction(std::move(function));
}

std::vector<ScriptFunctionPin> Targeted(std::vector<ScriptFunctionPin> inputs) {
    inputs.push_back({ "entity", ScriptValueType::Entity, false });
    return inputs;
}

} // namespace

bool ScriptUIApi::Register(ScriptRuntimeHost& host) {
    const auto applied = std::vector<ScriptFunctionPin>{ { "applied", ScriptValueType::Bool, true } };
    return RegisterFunction(host, "UI.Create", Targeted({
            { "parent", ScriptValueType::Hash, true },
            { "name", ScriptValueType::String, true },
            { "styleClass", ScriptValueType::String, false },
            { "visible", ScriptValueType::Bool, false },
        }), { { "element", ScriptValueType::Hash, true } }, &Create) &&
        RegisterFunction(host, "UI.Destroy", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Destroy) &&
        RegisterFunction(host, "UI.Show", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Show) &&
        RegisterFunction(host, "UI.Hide", Targeted({ { "element", ScriptValueType::Hash, true } }), applied, &Hide);
}

} // namespace kb::script
