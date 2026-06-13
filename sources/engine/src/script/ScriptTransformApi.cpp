#include "engine/script/ScriptTransformApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] kb::scene::SceneEntity EntityArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? kb::scene::SceneEntity{} : kb::scene::SceneEntity{ value->AsUInt64() };
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] bool Alive(const ScriptFunctionCallContext& context, kb::scene::SceneEntity entity) noexcept {
    return context.scene != nullptr && entity.IsValid() && context.scene->Entities().IsAlive(entity);
}

ScriptFunctionCallResult PositionResult(bool found, kb::scene::Vec3 position) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "found", ScriptValue{ found } },
            ScriptFunctionArgument{ "x", ScriptValue{ position.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ position.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ position.z } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

ScriptFunctionCallResult GetPosition(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return PositionResult(false, {});
    }
    const kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    return PositionResult(true, transform.localPosition);
}

ScriptFunctionCallResult SetPosition(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    transform.localPosition = kb::scene::Vec3{
        FloatArg(arguments, "x", transform.localPosition.x),
        FloatArg(arguments, "y", transform.localPosition.y),
        FloatArg(arguments, "z", transform.localPosition.z),
    };
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

ScriptFunctionCallResult Translate(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const kb::scene::SceneEntity entity = EntityArg(arguments, "entity");
    if (!Alive(context, entity)) {
        return BoolResult("moved", false);
    }
    kb::scene::TransformComponent transform = context.scene->Transforms().Get(entity);
    transform.localPosition.x += FloatArg(arguments, "x");
    transform.localPosition.y += FloatArg(arguments, "y");
    transform.localPosition.z += FloatArg(arguments, "z");
    context.scene->Transforms().Set(entity, transform);
    return BoolResult("moved", true);
}

bool RegisterFunction(
    ScriptRuntimeHost& host,
    std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptTransformApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Transform.GetPosition",
        { ScriptFunctionPin{ "entity", ScriptValueType::Entity, true } },
        {
            ScriptFunctionPin{ "found", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        &GetPosition) && ok;
    ok = RegisterFunction(host, "Transform.SetPosition",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &SetPosition) && ok;
    ok = RegisterFunction(host, "Transform.Translate",
        {
            ScriptFunctionPin{ "entity", ScriptValueType::Entity, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "moved", ScriptValueType::Bool, true } },
        &Translate) && ok;
    return ok;
}

} // namespace kb::script
