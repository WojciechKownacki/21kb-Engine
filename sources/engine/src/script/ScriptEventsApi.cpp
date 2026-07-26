#include "engine/script/ScriptEventsApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptEventBus.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace kb::script {
namespace {

[[nodiscard]] const ScriptValue* FindArg(
    std::span<const ScriptFunctionArgument> arguments,
    std::string_view name) noexcept {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

ScriptFunctionCallResult EmitFiltered(
    ScriptRuntimeHost& host,
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return ScriptFunctionCallResult{ .errors = { "Events.EmitFiltered requires an active scene" } };
    }
    const ScriptValue* eventValue = FindArg(arguments, "event");
    const std::string eventName = eventValue == nullptr ? std::string{} : eventValue->AsString();
    if (eventName.empty()) {
        return ScriptFunctionCallResult{ .errors = { "Events.EmitFiltered requires a non-empty event name" } };
    }

    EventRecipientFilter filter;
    if (const ScriptValue* value = FindArg(arguments, "tag")) {
        filter.tag = value->AsString();
    }
    if (const ScriptValue* value = FindArg(arguments, "component")) {
        filter.component = value->AsString();
    }
    if (const ScriptValue* value = FindArg(arguments, "scene")) {
        filter.sceneId = value->AsUInt64();
    }
    if (const ScriptValue* value = FindArg(arguments, "player")) {
        const std::int64_t player = value->AsInt64(-1);
        if (player < 0 || static_cast<std::uint64_t>(player) > std::numeric_limits<std::uint32_t>::max()) {
            return ScriptFunctionCallResult{ .errors = { "Events.EmitFiltered player must be a non-negative 32-bit integer" } };
        }
        filter.playerId = static_cast<std::uint32_t>(player);
    }
    if (const ScriptValue* value = FindArg(arguments, "channel")) {
        filter.channel = value->AsString();
    }

    kb::scene::SceneEntity target{};
    if (const ScriptValue* value = FindArg(arguments, "target")) {
        target = kb::scene::SceneEntity{ value->AsUInt64() };
    }
    ScriptEvent event{
        .name = std::move(eventName),
        .sender = context.caller,
        .target = target,
        .senderAsset = context.callerAsset,
    };
    ScriptEventDeliveryResult delivery = host.Runtime().Events().Emit(*context.scene, event, target, filter);
    if (!delivery.errors.empty()) {
        return ScriptFunctionCallResult{ .executed = false, .errors = std::move(delivery.errors) };
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "delivered", ScriptValue{ static_cast<int>(delivery.delivered) } },
        },
    };
}

} // namespace

bool ScriptEventsApi::Register(ScriptRuntimeHost& host) {
    ScriptFunctionDesc function;
    function.signature.name = "Events.EmitFiltered";
    function.signature.inputs = {
        ScriptFunctionPin{ "event", ScriptValueType::String, true },
        ScriptFunctionPin{ "target", ScriptValueType::Entity, false },
        ScriptFunctionPin{ "tag", ScriptValueType::String, false },
        ScriptFunctionPin{ "component", ScriptValueType::String, false },
        ScriptFunctionPin{ "scene", ScriptValueType::Hash, false },
        ScriptFunctionPin{ "player", ScriptValueType::Int64, false },
        ScriptFunctionPin{ "channel", ScriptValueType::String, false },
    };
    function.signature.outputs = {
        ScriptFunctionPin{ "delivered", ScriptValueType::Int, true },
    };
    function.callback = [&host](
                            const ScriptFunctionCallContext& context,
                            std::span<const ScriptFunctionArgument> arguments) {
        return EmitFiltered(host, context, arguments);
    };
    return host.RegisterFunction(std::move(function));
}

} // namespace kb::script
