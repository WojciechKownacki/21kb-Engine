#include "engine/script/ScriptNetApi.hpp"
#include "kb/net/Diagnostics.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <map>
#include <mutex>
#include <string_view>

namespace kb::script {

namespace {

using kb::network::INetworkScriptRuntime;
using kb::network::ScriptNetMode;
using kb::network::ScriptRpcArgKind;
using kb::network::ScriptRpcArgSpec;
using kb::network::ScriptRpcDeclaration;
using kb::network::ScriptRpcValue;
using Arguments = std::span<const ScriptFunctionArgument>;

constexpr std::size_t kMaxQueuedEvents = 1024U;
constexpr std::size_t kMaxRpcArguments = 8U;
constexpr std::size_t kNumberSlots = 4U;
constexpr std::size_t kStringSlots = 2U;
constexpr std::size_t kBoolSlots = 2U;
constexpr std::size_t kEntitySlots = 2U;

struct SceneNet {
    std::deque<ScriptNetEvent> events;
    kb::net::DiagLog diagnostics;
    std::array<std::uint64_t, 32U> drainedProblems{};
};

std::mutex g_mutex;
std::map<const kb::scene::Scene*, SceneNet> g_scenes;

[[nodiscard]] const ScriptValue* Find(Arguments arguments, std::string_view name) noexcept {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

// A failed call returns its text to the script and keeps it for the scene's report: a script that ignores the answer still shows up in it.
[[nodiscard]] ScriptFunctionCallResult Failure(const ScriptFunctionCallContext& context, std::string message, kb::net::DiagCode code = kb::net::DiagCode::RpcRejected) {
    if (context.scene != nullptr) {
        const std::lock_guard lock{ g_mutex };
        const auto found = g_scenes.find(context.scene);
        if (found != g_scenes.end()) {
            found->second.diagnostics.Report(code, message);
        }
    }
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

[[nodiscard]] ScriptFunctionCallResult Ok(std::vector<ScriptFunctionArgument> outputs) {
    return ScriptFunctionCallResult{ .executed = true, .outputs = std::move(outputs), .errors = {} };
}

[[nodiscard]] INetworkScriptRuntime* RuntimeOf(const ScriptFunctionCallContext& context) {
    return context.scene == nullptr ? nullptr : ScriptNetApi::Attached(*context.scene);
}

[[nodiscard]] bool ValidName(const std::string& name) {
    if (name.empty() || name.size() > 32U || !std::isalpha(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    return std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; });
}

[[nodiscard]] std::string Trim(std::string text) {
    const auto notSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

[[nodiscard]] bool ParseNumber(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(value);
}

const char* SlotPrefix(ScriptRpcArgKind kind) noexcept {
    switch (kind) {
    case ScriptRpcArgKind::Bool: return "b";
    case ScriptRpcArgKind::String: return "s";
    case ScriptRpcArgKind::Entity: return "e";
    default: return "n";
    }
}

[[nodiscard]] std::string SlotName(ScriptRpcArgKind kind, std::size_t index) {
    return std::string{ SlotPrefix(kind) } + std::to_string(index + 1U);
}

ScriptFunctionCallResult Mode(const ScriptFunctionCallContext& context, Arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    const ScriptNetMode mode = runtime == nullptr ? ScriptNetMode::Offline : runtime->Mode();
    return Ok({ { "mode", ScriptValue{ std::string{ mode == ScriptNetMode::Server ? "server" : mode == ScriptNetMode::Client ? "client" : "offline" } } },
        { "connected", ScriptValue{ runtime != nullptr && runtime->Connected() } },
        { "players", ScriptValue{ static_cast<int>(runtime == nullptr ? 0U : runtime->PlayerCount()) } } });
}

ScriptFunctionCallResult IsServer(const ScriptFunctionCallContext& context, Arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    return Ok({ { "value", ScriptValue{ runtime != nullptr && runtime->Mode() == ScriptNetMode::Server } } });
}

ScriptFunctionCallResult IsClient(const ScriptFunctionCallContext& context, Arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    return Ok({ { "value", ScriptValue{ runtime != nullptr && runtime->Mode() == ScriptNetMode::Client } } });
}

ScriptFunctionCallResult LocalPlayerId(const ScriptFunctionCallContext& context, Arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    return Ok({ { "player", ScriptValue{ static_cast<std::uint32_t>(runtime == nullptr ? 0U : runtime->LocalPlayerId()) } } });
}

ScriptFunctionCallResult RegisterRpc(const ScriptFunctionCallContext& context, Arguments arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    if (runtime == nullptr) {
        return Failure(context, "Net.RegisterRpc: no network session is attached to this scene (it runs offline: play it from a server and clients, or try it with `kb_cli net check`)");
    }
    ScriptRpcDeclaration declaration;
    declaration.name = Find(arguments, "name") != nullptr ? Find(arguments, "name")->AsString() : std::string{};
    if (!ValidName(declaration.name)) {
        return Failure(context, "Net.RegisterRpc: the name must be 1 to 32 letters, digits or underscores and start with a letter");
    }
    const std::string direction = Find(arguments, "direction") != nullptr ? Find(arguments, "direction")->AsString() : std::string{};
    if (direction != "toServer" && direction != "toClients") {
        return Failure(context, "Net.RegisterRpc '" + declaration.name + "': direction must be \"toServer\" or \"toClients\"");
    }
    declaration.toServer = direction == "toServer";
    if (const ScriptValue* reliable = Find(arguments, "reliable")) {
        declaration.reliable = reliable->AsBool(true);
    }
    std::string error;
    if (const ScriptValue* spec = Find(arguments, "args")) {
        if (!ScriptNetApi::ParseArgumentSpecs(spec->AsString(), declaration.args, error)) {
            return Failure(context, "Net.RegisterRpc '" + declaration.name + "': " + error);
        }
    }
    if (const ScriptValue* owner = Find(arguments, "owner")) {
        declaration.ownerArg = owner->AsInt(-1);
        const bool valid = declaration.ownerArg == -1 ||
            (declaration.toServer && declaration.ownerArg >= 0 && static_cast<std::size_t>(declaration.ownerArg) < declaration.args.size() &&
                declaration.args[static_cast<std::size_t>(declaration.ownerArg)].kind == ScriptRpcArgKind::Entity);
        if (!valid) {
            return Failure(context, "Net.RegisterRpc '" + declaration.name + "': owner must be -1 or the index of an entity argument of a toServer call");
        }
    }
    if (const ScriptValue* rate = Find(arguments, "maxPerSecond")) {
        const int perSecond = rate->AsInt(30);
        if (perSecond < 1 || perSecond > 1000) {
            return Failure(context, "Net.RegisterRpc '" + declaration.name + "': maxPerSecond must be between 1 and 1000");
        }
        declaration.maxCallsPerSecond = static_cast<std::uint32_t>(perSecond);
    }
    if (!runtime->DeclareRpc(declaration, error)) {
        return Failure(context, "Net.RegisterRpc '" + declaration.name + "': " + error);
    }
    return Ok({ { "registered", ScriptValue{ true } } });
}

ScriptFunctionCallResult Call(const ScriptFunctionCallContext& context, Arguments arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    if (runtime == nullptr) {
        return Failure(context, "Net.Call: no network session is attached to this scene (it runs offline: play it from a server and clients, or try it with `kb_cli net check`)");
    }
    const std::string name = Find(arguments, "rpc") != nullptr ? Find(arguments, "rpc")->AsString() : std::string{};
    const ScriptRpcDeclaration* declaration = runtime->FindRpc(name);
    if (declaration == nullptr) {
        return Failure(context, "Net.Call: no RPC named '" + name + "' was registered (declare it with Net.RegisterRpc in Ready, the same on the server and on every client)");
    }
    std::array<std::size_t, 4> next{};
    std::vector<ScriptRpcValue> values;
    std::vector<std::string> used;
    for (const ScriptRpcArgSpec& spec : declaration->args) {
        const std::size_t prefixIndex = spec.kind == ScriptRpcArgKind::Bool ? 0U : spec.kind == ScriptRpcArgKind::String ? 1U : spec.kind == ScriptRpcArgKind::Entity ? 2U : 3U;
        const std::string slot = SlotName(spec.kind, next[prefixIndex]++);
        const ScriptValue* value = Find(arguments, slot);
        if (value == nullptr) {
            return Failure(context, "Net.Call '" + name + "': missing argument " + slot);
        }
        used.push_back(slot);
        switch (spec.kind) {
        case ScriptRpcArgKind::Bool:
            values.emplace_back(value->AsBool());
            break;
        case ScriptRpcArgKind::String: {
            const std::string text = value->AsString();
            if (text.size() > spec.maxLength) {
                return Failure(context, "Net.Call '" + name + "': " + slot + " is longer than " + std::to_string(spec.maxLength) + " bytes");
            }
            values.emplace_back(text);
            break;
        }
        case ScriptRpcArgKind::Entity: {
            const std::uint64_t entity = value->AsUInt64();
            if (entity == 0U) {
                return Failure(context, "Net.Call '" + name + "': " + slot + " is not an entity");
            }
            values.emplace_back(entity);
            break;
        }
        default: {
            const double number = value->AsDouble();
            if (!std::isfinite(number) || number < spec.minimum || number > spec.maximum) {
                return Failure(context, "Net.Call '" + name + "': " + slot + " is outside " + std::to_string(spec.minimum) + ".." + std::to_string(spec.maximum));
            }
            if (spec.kind == ScriptRpcArgKind::Float) {
                values.emplace_back(static_cast<float>(number));
            } else if (number != std::floor(number)) {
                return Failure(context, "Net.Call '" + name + "': " + slot + " must be a whole number");
            } else if (spec.kind == ScriptRpcArgKind::Int) {
                if (number < -2147483648.0 || number > 2147483647.0) {
                    return Failure(context, "Net.Call '" + name + "': " + slot + " does not fit 32 bits");
                }
                values.emplace_back(static_cast<std::int32_t>(number));
            } else {
                if (number < 0.0 || number > 4294967295.0) {
                    return Failure(context, "Net.Call '" + name + "': " + slot + " does not fit an unsigned 32 bit value");
                }
                values.emplace_back(static_cast<std::uint32_t>(number));
            }
            break;
        }
        }
    }
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name != "rpc" && argument.name != "target" && std::ranges::find(used, argument.name) == used.end()) {
            return Failure(context, "Net.Call '" + name + "': the RPC does not take an argument " + argument.name);
        }
    }
    std::uint64_t target = 0U;
    if (const ScriptValue* value = Find(arguments, "target")) {
        target = value->AsUInt32();
    }
    std::string error;
    if (!runtime->CallRpc(name, target, values, error)) {
        return Failure(context, "Net.Call '" + name + "': " + error);
    }
    return Ok({ { "sent", ScriptValue{ true } } });
}

ScriptFunctionCallResult Replicate(const ScriptFunctionCallContext& context, Arguments arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    if (runtime == nullptr || runtime->Mode() != ScriptNetMode::Server) {
        return Failure(context, "Net.Replicate: only a server can replicate entities (the server shows its objects to the players; on a client guard it with `if Net.IsServer() then`)", kb::net::DiagCode::NotAuthority);
    }
    const ScriptValue* entity = Find(arguments, "entity");
    std::string error;
    const ScriptValue* mode = Find(arguments, "mode");
    const std::string selected = mode == nullptr ? "spatial" : mode->AsString();
    if (selected != "spatial" && selected != "global") return Failure(context, "Net.Replicate: mode must be spatial or global");
    if (entity == nullptr || !runtime->Replicate(entity->AsUInt64(), error, selected == "global")) {
        return Failure(context, "Net.Replicate: " + (error.empty() ? std::string{ "no entity" } : error), kb::net::DiagCode::EntityLimit);
    }
    return Ok({ { "replicated", ScriptValue{ true } } });
}

ScriptFunctionCallResult IsReplicated(const ScriptFunctionCallContext& context, Arguments arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    const ScriptValue* entity = Find(arguments, "entity");
    return Ok({ { "value", ScriptValue{ runtime != nullptr && entity != nullptr && runtime->IsReplicated(entity->AsUInt64()) } } });
}

ScriptFunctionCallResult SetOwner(const ScriptFunctionCallContext& context, Arguments arguments) {
    INetworkScriptRuntime* runtime = RuntimeOf(context);
    if (runtime == nullptr || runtime->Mode() != ScriptNetMode::Server) {
        return Failure(context, "Net.SetOwner: only a server can change the owner of an entity (guard it with `if Net.IsServer() then`)");
    }
    const ScriptValue* entity = Find(arguments, "entity");
    const ScriptValue* player = Find(arguments, "player");
    std::string error;
    if (entity == nullptr || player == nullptr || !runtime->SetOwner(entity->AsUInt64(), player->AsUInt32(), error)) {
        return Failure(context, "Net.SetOwner: " + (error.empty() ? std::string{ "no entity or player" } : error));
    }
    return Ok({ { "applied", ScriptValue{ true } } });
}

ScriptFunctionCallResult SetVar(const ScriptFunctionCallContext& context, Arguments arguments) {
    auto* runtime = RuntimeOf(context);
    const auto* name = Find(arguments, "name");
    const auto* entity = Find(arguments, "entity");
    const auto* value = Find(arguments, "value");
    const std::string label = name == nullptr ? "" : name->AsString();
    if (runtime == nullptr || runtime->Mode() != ScriptNetMode::Server) return Failure(context, "Net.SetVar '" + label + "': only a server can set variables", kb::net::DiagCode::NotAuthority);
    std::string error;
    if (entity == nullptr || value == nullptr || !runtime->SetVar(entity->AsUInt64(), label, value->AsDouble(), error)) return Failure(context, "Net.SetVar: " + error);
    return Ok({{"applied", ScriptValue{true}}});
}

ScriptFunctionCallResult GetVar(const ScriptFunctionCallContext& context, Arguments arguments) {
    auto* runtime = RuntimeOf(context);
    const auto* name = Find(arguments, "name");
    const auto* entity = Find(arguments, "entity");
    const std::string label = name == nullptr ? "" : name->AsString();
    if (runtime == nullptr) return Failure(context, "Net.GetVar '" + label + "': no network runtime is attached");
    std::string error;
    double value = 0.0;
    if (entity == nullptr || !runtime->GetVar(entity->AsUInt64(), label, value, error)) return Failure(context, "Net.GetVar: " + error);
    return Ok({{"value", ScriptValue{value}}});
}

} // namespace

bool ScriptNetApi::ParseArgumentSpecs(const std::string& text, std::vector<ScriptRpcArgSpec>& specs, std::string& error) {
    specs.clear();
    if (Trim(text).empty()) {
        return true;
    }
    std::array<std::size_t, 4> counts{}; // bool, string, entity, number
    std::size_t position = 0U;
    while (position <= text.size()) {
        std::size_t comma = text.find(',', position);
        if (comma == std::string::npos) {
            comma = text.size();
        }
        const std::string token = Trim(text.substr(position, comma - position));
        position = comma + 1U;
        if (token.empty()) {
            error = "the argument list has an empty entry";
            return false;
        }
        std::string kind = token;
        std::string range;
        if (const std::size_t open = token.find('('); open != std::string::npos) {
            if (token.back() != ')') {
                error = "'" + token + "' is missing a closing bracket";
                return false;
            }
            kind = Trim(token.substr(0U, open));
            range = Trim(token.substr(open + 1U, token.size() - open - 2U));
        }
        ScriptRpcArgSpec spec;
        if (kind == "bool" || kind == "entity") {
            if (!range.empty() || token.find('(') != std::string::npos) {
                error = "'" + kind + "' takes no range";
                return false;
            }
            spec.kind = kind == "bool" ? ScriptRpcArgKind::Bool : ScriptRpcArgKind::Entity;
        } else if (kind == "string") {
            spec.kind = ScriptRpcArgKind::String;
            if (token.find('(') != std::string::npos) {
                double length = 0.0;
                if (!ParseNumber(range, length) || length < 1.0 || length > 512.0 || length != std::floor(length)) {
                    error = "a string length must be a whole number from 1 to 512";
                    return false;
                }
                spec.maxLength = static_cast<std::uint16_t>(length);
            }
        } else if (kind == "int" || kind == "uint" || kind == "float") {
            spec.kind = kind == "int" ? ScriptRpcArgKind::Int : kind == "uint" ? ScriptRpcArgKind::UInt : ScriptRpcArgKind::Float;
            if (kind == "uint") {
                spec.minimum = 0.0;
                spec.maximum = 4294967295.0;
            } else if (kind == "int") {
                spec.minimum = -2147483648.0;
                spec.maximum = 2147483647.0;
            } else {
                spec.minimum = -1.0e9;
                spec.maximum = 1.0e9;
            }
            if (token.find('(') != std::string::npos) {
                const std::size_t dots = range.find("..");
                double low = 0.0;
                double high = 0.0;
                if (dots == std::string::npos || !ParseNumber(Trim(range.substr(0U, dots)), low) || !ParseNumber(Trim(range.substr(dots + 2U)), high) || low > high) {
                    error = "a range must look like (min..max) with min not above max";
                    return false;
                }
                spec.minimum = std::max(spec.minimum, low);
                spec.maximum = std::min(spec.maximum, high);
                if (spec.minimum > spec.maximum) {
                    error = "the range of '" + kind + "' is outside what the type holds";
                    return false;
                }
            }
        } else {
            error = "unknown argument type '" + kind + "' (use bool, int, uint, float, string, entity)";
            return false;
        }
        const std::size_t group = spec.kind == ScriptRpcArgKind::Bool ? 0U : spec.kind == ScriptRpcArgKind::String ? 1U : spec.kind == ScriptRpcArgKind::Entity ? 2U : 3U;
        const std::size_t limit = group == 0U ? kBoolSlots : group == 1U ? kStringSlots : group == 2U ? kEntitySlots : kNumberSlots;
        if (++counts[group] > limit) {
            error = std::string{ "too many " } + (group == 0U ? "bool" : group == 1U ? "string" : group == 2U ? "entity" : "number") + " arguments (at most " + std::to_string(limit) + ")";
            return false;
        }
        specs.push_back(spec);
        if (specs.size() > kMaxRpcArguments) {
            error = "an RPC takes at most 8 arguments";
            return false;
        }
        if (comma == text.size()) {
            break;
        }
    }
    return true;
}

void ScriptNetApi::Attach(kb::scene::Scene& scene, INetworkScriptRuntime* runtime) {
    const std::lock_guard lock{ g_mutex };
    kb::scene::SceneAccess::State(scene).networkRuntime = runtime;
    if (runtime == nullptr) {
        g_scenes.erase(&scene);
    } else {
        static_cast<void>(g_scenes[&scene]);
    }
}

INetworkScriptRuntime* ScriptNetApi::Attached(const kb::scene::Scene& scene) {
    const std::lock_guard lock{ g_mutex };
    return kb::scene::SceneAccess::State(scene).networkRuntime;
}

void ScriptNetApi::QueueEvent(const kb::scene::Scene& scene, ScriptNetEvent event) {
    const std::lock_guard lock{ g_mutex };
    const auto found = g_scenes.find(&scene);
    if (found == g_scenes.end()) {
        return;
    }
    SceneNet& state = found->second;
    if (event.name == "OnNetDespawned") {
        const auto entityOf = [](const ScriptNetEvent& queued) -> const std::uint64_t* {
            for (const auto& [name, value] : queued.arguments) {
                if (name == "entity") return std::get_if<std::uint64_t>(&value);
            }
            return nullptr;
        };
        if (const auto* entity = entityOf(event)) {
            std::erase_if(state.events, [&](const ScriptNetEvent& queued) {
                const auto* spawned = entityOf(queued);
                return queued.name == "OnNetSpawned" && spawned != nullptr && *spawned == *entity;
            });
        }
    }
    if (state.events.size() >= kMaxQueuedEvents) {
        const auto victim = std::find_if(state.events.begin(), state.events.end(), [](const ScriptNetEvent& queued) {
            return queued.name == "OnNetSpawned" || queued.name == "OnNetDespawned";
        });
        if (victim != state.events.end()) {
            state.events.erase(victim);
            state.diagnostics.Report(kb::net::DiagCode::EventsDropped, "script network events were dropped because the scripts did not keep up (" + std::to_string(kMaxQueuedEvents) + " wait at once)");
        } else if (event.name != "OnNetConnected" && event.name != "OnNetDisconnected") {
            state.diagnostics.Report(kb::net::DiagCode::EventsDropped, "the script event queue is full; the new event was dropped");
            return;
        }
    }
    found->second.events.push_back(std::move(event));
}

std::vector<std::string> ScriptNetApi::DrainProblems(const kb::scene::Scene& scene) {
    const std::lock_guard lock{ g_mutex };
    const auto found = g_scenes.find(&scene);
    std::vector<std::string> drained;
    if (found != g_scenes.end()) {
        SceneNet& state = found->second;
        std::size_t index = 0U;
        for (const auto& entry : state.diagnostics.Entries()) {
            if (state.drainedProblems[index] != entry.count) {
                drained.emplace_back(std::string(kb::net::DiagName(entry.code)) + ": " + entry.detail);
                state.drainedProblems[index] = entry.count;
            }
            ++index;
        }
    }
    return drained;
}

std::vector<ScriptNetEvent> ScriptNetApi::DrainEvents(const kb::scene::Scene& scene) {
    const std::lock_guard lock{ g_mutex };
    const auto found = g_scenes.find(&scene);
    std::vector<ScriptNetEvent> drained;
    if (found != g_scenes.end()) {
        drained.assign(std::make_move_iterator(found->second.events.begin()), std::make_move_iterator(found->second.events.end()));
        found->second.events.clear();
    }
    return drained;
}

bool ScriptNetApi::Register(ScriptRuntimeHost& host) {
    using T = ScriptValueType;
    const auto add = [&host](std::string name, std::string description, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs,
                          ScriptFunctionCallback callback) {
        ScriptFunctionDesc desc;
        desc.signature.name = std::move(name);
        desc.signature.description = std::move(description);
        desc.signature.inputs = std::move(inputs);
        desc.signature.outputs = std::move(outputs);
        desc.callback = std::move(callback);
        return host.RegisterFunction(std::move(desc));
    };
    bool ok = add("Net.Mode", "Reports whether this scene is offline, a server or a client, whether the session is connected, and how many players a server has.", {},
        { { "mode", T::String, true }, { "connected", T::Bool, true }, { "players", T::Int, true } }, &Mode);
    ok = add("Net.IsServer", "True when this scene runs as the authoritative server.", {}, { { "value", T::Bool, true } }, &IsServer) && ok;
    ok = add("Net.IsClient", "True when this scene runs as a client of a server.", {}, { { "value", T::Bool, true } }, &IsClient) && ok;
    ok = add("Net.LocalPlayerId", "The player id of this machine's player on a client; 0 on a server or offline.", {}, { { "player", T::UInt32, true } }, &LocalPlayerId) && ok;
    ok = add("Net.RegisterRpc",
        "Declares a remote procedure call before the session starts (call it from Created or Ready). direction is \"toServer\" or \"toClients\"; args lists the types, for example "
        "\"float(-1..1),string(32),entity\" (at most 4 numbers, 2 strings, 2 bools, 2 entities); owner is the index of an entity argument that the caller must own, or -1. "
        "A call is delivered to the receiving scene's scripts as the event OnRpc_<name>.",
        { { "name", T::String, true }, { "direction", T::String, true }, { "args", T::String, false }, { "reliable", T::Bool, false }, { "owner", T::Int, false },
            { "maxPerSecond", T::Int, false } },
        { { "registered", T::Bool, true } }, &RegisterRpc) && ok;
    ok = add("Net.Call",
        "Calls a registered RPC. The arguments go in the slots n1..n4 (numbers), s1..s2 (strings), b1..b2 (bools) and e1..e2 (entities), in the order of the declaration. "
        "target is the player to call from a server (0: every player); from a client it must be 0.",
        { { "rpc", T::String, true }, { "target", T::UInt32, false }, { "n1", T::Double, false }, { "n2", T::Double, false }, { "n3", T::Double, false },
            { "n4", T::Double, false }, { "s1", T::String, false }, { "s2", T::String, false }, { "b1", T::Bool, false }, { "b2", T::Bool, false },
            { "e1", T::Entity, false }, { "e2", T::Entity, false } },
        { { "sent", T::Bool, true } }, &Call) && ok;
    ok = add("Net.Replicate", "Marks an entity to be replicated from the server to its clients. Server only.", { { "entity", T::Entity, true }, { "mode", T::String, false } }, { { "replicated", T::Bool, true } },
        &Replicate) && ok;
    ok = add("Net.IsReplicated", "True when the entity is replicated by this server.", { { "entity", T::Entity, true } }, { { "value", T::Bool, true } }, &IsReplicated) && ok;
    ok = add("Net.SetOwner", "Gives an entity to a player, who may then call RPCs that act on it. Server only.", { { "entity", T::Entity, true }, { "player", T::UInt32, true } },
        { { "applied", T::Bool, true } }, &SetOwner) && ok;
    ok = add("Net.SetVar", "Sets a variable declared in Config/Replication.json. Server only; rejects unknown names, missing components and values outside the declared range. Bool variables use 0 or 1.",
        {{"entity", T::Entity, true}, {"name", T::String, true}, {"value", T::Double, true}}, {{"applied", T::Bool, true}}, &SetVar) && ok;
    ok = add("Net.GetVar", "Reads a declared network variable available to this scene. Bool variables return 0 or 1.",
        {{"entity", T::Entity, true}, {"name", T::String, true}}, {{"value", T::Double, true}}, &GetVar) && ok;
    return ok;
}

} // namespace kb::script
