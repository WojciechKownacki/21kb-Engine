#include "engine/script/ScriptInputApi.hpp"

#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <charconv>
#include <cstdint>
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

std::string ActionName(std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = FindArg(arguments, "action");
    return value != nullptr ? value->AsString() : std::string{};
}

ScriptFunctionCallResult NoScene() {
    return ScriptFunctionCallResult{.executed = false, .outputs = {}, .errors = {"input api requires an active scene"}};
}

// Helper to build a one-output boolean result.
ScriptFunctionCallResult BoolResult(std::string_view pin, bool value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {ScriptFunctionArgument{std::string{pin}, ScriptValue{value}}},
        .errors = {}};
}

bool RegisterActionQuery(ScriptRuntimeHost& host, std::string name, std::string outputPin,
                         bool (kb::input::InputSubsystem::*query)(std::string_view) const) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}};
    desc.signature.outputs = {ScriptFunctionPin{outputPin, ScriptValueType::Bool, true}};
    desc.callback = [outputPin, query](const ScriptFunctionCallContext& context,
                                       std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const bool value = (context.scene->Input().*query)(ActionName(arguments));
        return BoolResult(outputPin, value);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterValueQuery(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}};
    desc.signature.outputs = {ScriptFunctionPin{"value", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input().GetActionValue(ActionName(arguments));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"value", ScriptValue{value.AsAxis1D()}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

// Reads the action's raw/modified value as a bool (value.AsBool(), i.e. x != 0),
// distinct from Held/Pressed/Released below: those reflect whether a *trigger*
// fired (respecting deadzones, Hold thresholds, etc. - see InputMappingEvaluator),
// while ActionBool reflects the value itself, mirroring Unreal's direct
// FInputActionValue::Get<bool>() read versus binding to a trigger event.
bool RegisterActionBoolQuery(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}};
    desc.signature.outputs = {ScriptFunctionPin{"value", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input().GetActionValue(ActionName(arguments));
        return BoolResult("value", value.AsBool());
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterValueQueryXY(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}};
    desc.signature.outputs = {ScriptFunctionPin{"x", ScriptValueType::Float, true},
                              ScriptFunctionPin{"y", ScriptValueType::Float, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input().GetActionValue(ActionName(arguments));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"x", ScriptValue{value.x}},
                        ScriptFunctionArgument{"y", ScriptValue{value.y}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterValueQueryXYZ(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"action", ScriptValueType::String, true}};
    desc.signature.outputs = {
        ScriptFunctionPin{"x", ScriptValueType::Float, true},
        ScriptFunctionPin{"y", ScriptValueType::Float, true},
        ScriptFunctionPin{"z", ScriptValueType::Float, true},
    };
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const kb::input::InputValue value = context.scene->Input().GetActionValue(ActionName(arguments));
        return ScriptFunctionCallResult{
            .executed = true,
            .outputs = {ScriptFunctionArgument{"x", ScriptValue{value.x}},
                        ScriptFunctionArgument{"y", ScriptValue{value.y}},
                        ScriptFunctionArgument{"z", ScriptValue{value.z}}},
            .errors = {}};
    };
    return host.RegisterFunction(std::move(desc));
}

[[nodiscard]] std::uint64_t ParseAssetId(std::string_view text) {
    std::uint64_t id = 0U;
    std::from_chars(text.data(), text.data() + text.size(), id);
    return id;
}

bool RegisterAddMappingContext(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"context", ScriptValueType::String, true},
                             ScriptFunctionPin{"priority", ScriptValueType::Int, false}};
    desc.signature.outputs = {ScriptFunctionPin{"added", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const ScriptValue* priorityArg = FindArg(arguments, "priority");
        const std::uint64_t id = contextArg != nullptr ? ParseAssetId(contextArg->AsString()) : 0U;
        const auto priority = static_cast<std::int32_t>(priorityArg != nullptr ? priorityArg->AsInt() : 0);
        const bool added = context.scene->Input().AddMappingContext(id, priority);
        return BoolResult("added", added);
    };
    return host.RegisterFunction(std::move(desc));
}

bool RegisterRemoveMappingContext(ScriptRuntimeHost& host, std::string name) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {ScriptFunctionPin{"context", ScriptValueType::String, true}};
    desc.signature.outputs = {ScriptFunctionPin{"removed", ScriptValueType::Bool, true}};
    desc.callback = [](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
        if (context.scene == nullptr) {
            return NoScene();
        }
        const ScriptValue* contextArg = FindArg(arguments, "context");
        const std::uint64_t id = contextArg != nullptr ? ParseAssetId(contextArg->AsString()) : 0U;
        const bool had = context.scene->Input().HasMappingContext(id);
        context.scene->Input().RemoveMappingContext(id);
        return BoolResult("removed", had);
    };
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptInputApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterActionQuery(host, "Input.IsPressed", "pressed", &kb::input::InputSubsystem::IsActionPressed) && ok;
    ok = RegisterActionQuery(host, "Input.WasPressed", "pressed", &kb::input::InputSubsystem::WasActionStarted) && ok;
    ok = RegisterActionQuery(host, "Input.WasReleased", "released", &kb::input::InputSubsystem::WasActionReleased) && ok;
    ok = RegisterValueQuery(host, "Input.Value") && ok;
    ok = RegisterValueQueryXY(host, "Input.Vector2") && ok;
    ok = RegisterValueQueryXYZ(host, "Input.Vector3") && ok;
    ok = RegisterAddMappingContext(host, "Input.AddMappingContext") && ok;
    ok = RegisterRemoveMappingContext(host, "Input.RemoveMappingContext") && ok;

    ok = RegisterActionBoolQuery(host, "Input.ActionBool") && ok;
    ok = RegisterValueQuery(host, "Input.ActionFloat") && ok;
    ok = RegisterValueQueryXY(host, "Input.Action2D") && ok;
    ok = RegisterActionQuery(host, "Input.Pressed", "pressed", &kb::input::InputSubsystem::WasActionStarted) && ok;
    ok = RegisterActionQuery(host, "Input.Released", "released", &kb::input::InputSubsystem::WasActionReleased) && ok;
    ok = RegisterActionQuery(host, "Input.Held", "held", &kb::input::InputSubsystem::IsActionPressed) && ok;

    ok = RegisterActionQuery(host, "IsActionPressed", "pressed", &kb::input::InputSubsystem::IsActionPressed) && ok;
    ok = RegisterActionQuery(host, "WasActionStarted", "started", &kb::input::InputSubsystem::WasActionStarted) && ok;
    ok = RegisterActionQuery(host, "WasActionTriggered", "triggered", &kb::input::InputSubsystem::WasActionTriggered) && ok;
    ok = RegisterActionQuery(host, "WasActionCompleted", "completed", &kb::input::InputSubsystem::WasActionCompleted) && ok;
    ok = RegisterActionQuery(host, "WasActionReleased", "released", &kb::input::InputSubsystem::WasActionReleased) && ok;
    ok = RegisterValueQuery(host, "GetActionValue") && ok;
    ok = RegisterValueQueryXY(host, "GetActionValueXY") && ok;
    ok = RegisterValueQueryXYZ(host, "GetActionValueXYZ") && ok;
    ok = RegisterAddMappingContext(host, "AddMappingContext") && ok;
    ok = RegisterRemoveMappingContext(host, "RemoveMappingContext") && ok;
    return ok;
}

} // namespace kb::script
