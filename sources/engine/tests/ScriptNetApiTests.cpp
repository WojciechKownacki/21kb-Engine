#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/network/NetworkScriptBridge.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptNetApi.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const std::string& message) {
    kb::tests::Require(condition, message.c_str());
}

using namespace kb::network;
using kb::script::ScriptFunctionArgument;
using kb::script::ScriptFunctionCallResult;
using kb::script::ScriptNetApi;
using kb::script::ScriptValue;

// A network session that only records what scripts ask of it.
class FakeNet final : public INetworkScriptRuntime {
public:
    ScriptNetMode Mode() const override { return mode; }
    bool Connected() const override { return connected; }
    std::uint64_t LocalPlayerId() const override { return player; }
    std::size_t PlayerCount() const override { return players; }
    bool DeclareRpc(const ScriptRpcDeclaration& declaration, std::string& error) override {
        if (frozen) {
            error = "the session has started";
            return false;
        }
        if (declared.contains(declaration.name)) {
            error = "already registered";
            return false;
        }
        declared[declaration.name] = declaration;
        return true;
    }
    const ScriptRpcDeclaration* FindRpc(const std::string& name) const override {
        const auto found = declared.find(name);
        return found == declared.end() ? nullptr : &found->second;
    }
    bool CallRpc(const std::string& name, std::uint64_t target, std::span<const ScriptRpcValue> args, std::string&) override {
        calls.push_back({ name, target, std::vector<ScriptRpcValue>(args.begin(), args.end()) });
        return true;
    }
    bool Replicate(std::uint64_t entity, std::string& error) override {
        if (mode != ScriptNetMode::Server) {
            error = "not a server";
            return false;
        }
        replicated.push_back(entity);
        return true;
    }
    bool IsReplicated(std::uint64_t entity) const override {
        for (const std::uint64_t id : replicated) {
            if (id == entity) {
                return true;
            }
        }
        return false;
    }
    bool SetOwner(std::uint64_t entity, std::uint64_t newOwner, std::string&) override {
        owners[entity] = newOwner;
        return true;
    }

    struct Call {
        std::string name;
        std::uint64_t target;
        std::vector<ScriptRpcValue> args;
    };
    ScriptNetMode mode = ScriptNetMode::Server;
    bool connected = true;
    bool frozen = false;
    std::uint64_t player = 0U;
    std::size_t players = 2U;
    std::map<std::string, ScriptRpcDeclaration> declared;
    std::vector<Call> calls;
    std::vector<std::uint64_t> replicated;
    std::map<std::uint64_t, std::uint64_t> owners;
};

ScriptFunctionCallResult CallNet(kb::script::ScriptRuntimeHost& host, kb::scene::Scene& scene, const char* name, std::vector<ScriptFunctionArgument> arguments = {}) {
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    return host.Functions().Call(name, arguments, context);
}

ScriptFunctionArgument Arg(const char* name, ScriptValue value) {
    return ScriptFunctionArgument{ name, std::move(value) };
}

bool Failed(const ScriptFunctionCallResult& result, const char* text) {
    if (result.Succeeded()) {
        return false;
    }
    for (const std::string& error : result.errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void RunSpecParserTest() {
    std::vector<ScriptRpcArgSpec> specs;
    std::string error;
    Check(ScriptNetApi::ParseArgumentSpecs("", specs, error) && specs.empty(), "an empty argument list must be valid");
    Check(ScriptNetApi::ParseArgumentSpecs("float(-1..1), string(32),entity , bool,int(0..100),uint", specs, error) && specs.size() == 6U, "a valid argument list was refused: " + error);
    Check(specs[0].kind == ScriptRpcArgKind::Float && specs[0].minimum == -1.0 && specs[0].maximum == 1.0, "a float range was not read");
    Check(specs[1].kind == ScriptRpcArgKind::String && specs[1].maxLength == 32U, "a string length was not read");
    Check(specs[4].kind == ScriptRpcArgKind::Int && specs[4].minimum == 0.0 && specs[4].maximum == 100.0, "an int range was not read");
    Check(specs[5].kind == ScriptRpcArgKind::UInt && specs[5].minimum == 0.0 && specs[5].maximum == 4294967295.0, "a uint has the wrong default range");
    Check(ScriptNetApi::ParseArgumentSpecs("float(0.5..1.5)", specs, error) && specs[0].minimum == 0.5 && specs[0].maximum == 1.5, "a decimal range was not read");
    for (const char* bad : { "double", "float(1..0)", "float(1)", "float(a..b)", "bool(1)", "entity(2)", "string(0)", "string(600)", "string(x)", "float(1..2", "int,,int", ",", "uint(-5..-1)",
             "float,float,float,float,float", "string,string,string", "bool,bool,bool", "entity,entity,entity", "int,int,int,int,int,int,int,int,int" }) {
        Check(!ScriptNetApi::ParseArgumentSpecs(bad, specs, error) && !error.empty(), std::string{ "a bad argument list was accepted: " } + bad);
    }
}

void RunOfflineTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    Check(host.Succeeded(), "the script host did not initialize");
    for (const char* name : { "Net.Mode", "Net.IsServer", "Net.IsClient", "Net.LocalPlayerId", "Net.RegisterRpc", "Net.Call", "Net.Replicate", "Net.IsReplicated", "Net.SetOwner" }) {
        Check(host.Functions().FindSignature(name) != nullptr, std::string{ "Net function was not registered: " } + name);
    }
    Check(ScriptNetApi::Attached(scene) == nullptr, "a fresh scene has a network session");
    const ScriptFunctionCallResult mode = CallNet(host, scene, "Net.Mode");
    Check(mode.Succeeded() && mode.Output("mode")->AsString() == "offline" && !mode.Output("connected")->AsBool() && mode.Output("players")->AsInt() == 0,
        "an offline scene reports a session");
    Check(!CallNet(host, scene, "Net.IsServer").Output("value")->AsBool() && !CallNet(host, scene, "Net.IsClient").Output("value")->AsBool() &&
            CallNet(host, scene, "Net.LocalPlayerId").Output("player")->AsUInt32() == 0U,
        "an offline scene claims to be a server or a client");
    Check(Failed(CallNet(host, scene, "Net.Call", { Arg("rpc", ScriptValue{ std::string{ "Fire" } }) }), "no network session"), "Net.Call offline did not explain itself");
    Check(Failed(CallNet(host, scene, "Net.RegisterRpc", { Arg("name", ScriptValue{ std::string{ "Fire" } }), Arg("direction", ScriptValue{ std::string{ "toServer" } }) }), "no network session"),
        "Net.RegisterRpc offline did not explain itself");
    Check(Failed(CallNet(host, scene, "Net.Replicate", { Arg("entity", ScriptValue{ std::uint64_t{ 1U }, kb::script::ScriptValueType::Entity }) }), "only a server"),
        "Net.Replicate offline did not explain itself");
    // Events queued for a scene that has no session are dropped, not kept forever.
    ScriptNetApi::QueueEvent(scene, kb::script::ScriptNetEvent{ "OnNetConnected", {} });
    Check(ScriptNetApi::DrainEvents(scene).empty(), "events were queued for a scene without a session");
}

void RunDeclareAndCallTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    FakeNet net;
    ScriptNetApi::Attach(scene, &net);
    Check(ScriptNetApi::Attached(scene) == &net, "the session was not attached");

    const ScriptFunctionCallResult mode = CallNet(host, scene, "Net.Mode");
    Check(mode.Output("mode")->AsString() == "server" && mode.Output("connected")->AsBool() && mode.Output("players")->AsInt() == 2, "Net.Mode does not report the session");
    Check(CallNet(host, scene, "Net.IsServer").Output("value")->AsBool(), "Net.IsServer is false on a server");

    const auto declare = [&](const char* name, const char* direction, const char* args, std::vector<ScriptFunctionArgument> extra = {}) {
        std::vector<ScriptFunctionArgument> arguments{ Arg("name", ScriptValue{ std::string{ name } }), Arg("direction", ScriptValue{ std::string{ direction } }),
            Arg("args", ScriptValue{ std::string{ args } }) };
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        return CallNet(host, scene, "Net.RegisterRpc", arguments);
    };
    Check(declare("Fire", "toServer", "entity,float(-1..1),float(-1..1),string(16),bool,int(0..9)", { Arg("owner", ScriptValue{ 0 }), Arg("maxPerSecond", ScriptValue{ 20 }) }).Succeeded(),
        "a valid declaration was refused");
    const ScriptRpcDeclaration* fire = net.FindRpc("Fire");
    Check(fire != nullptr && fire->toServer && fire->reliable && fire->ownerArg == 0 && fire->maxCallsPerSecond == 20U && fire->args.size() == 6U, "the declaration did not reach the session");
    Check(declare("Announce", "toClients", "string(64)", { Arg("reliable", ScriptValue{ false }) }).Succeeded() && !net.FindRpc("Announce")->reliable, "an unreliable declaration was refused");

    // Refused declarations name the problem.
    Check(Failed(declare("Fire", "toServer", ""), "already registered"), "a repeated name was accepted");
    Check(Failed(declare("9bad", "toServer", ""), "must be 1 to 32"), "a bad name was accepted");
    Check(Failed(declare("Bad name", "toServer", ""), "must be 1 to 32"), "a name with a space was accepted");
    Check(Failed(declare("Sideways", "sideways", ""), "direction"), "a bad direction was accepted");
    Check(Failed(declare("BadArgs", "toServer", "float(1..0)"), "range"), "a bad argument list was accepted");
    Check(Failed(declare("BadOwner", "toServer", "float", { Arg("owner", ScriptValue{ 0 }) }), "owner"), "an owner that is not an entity was accepted");
    Check(Failed(declare("ServerOwner", "toClients", "entity", { Arg("owner", ScriptValue{ 0 }) }), "owner"), "an owner on a server call was accepted");
    Check(Failed(declare("Fast", "toServer", "", { Arg("maxPerSecond", ScriptValue{ 5000 }) }), "maxPerSecond"), "a huge rate was accepted");
    net.frozen = true;
    Check(Failed(declare("Late", "toServer", ""), "has started"), "a declaration after the session started was accepted");
    net.frozen = false;

    // Calls: the slots follow the declaration, values are checked before they leave the script.
    net.mode = ScriptNetMode::Client;
    net.player = 7U;
    Check(CallNet(host, scene, "Net.LocalPlayerId").Output("player")->AsUInt32() == 7U && CallNet(host, scene, "Net.IsClient").Output("value")->AsBool(), "a client does not know who it is");
    const auto fireCall = [&](std::vector<ScriptFunctionArgument> arguments) {
        arguments.insert(arguments.begin(), Arg("rpc", ScriptValue{ std::string{ "Fire" } }));
        return CallNet(host, scene, "Net.Call", arguments);
    };
    const std::vector<ScriptFunctionArgument> valid{ Arg("e1", ScriptValue{ std::uint64_t{ 42U }, kb::script::ScriptValueType::Entity }), Arg("n1", ScriptValue{ 0.5 }), Arg("n2", ScriptValue{ -1.0 }),
        Arg("s1", ScriptValue{ std::string{ "hi" } }), Arg("b1", ScriptValue{ true }), Arg("n3", ScriptValue{ 7.0 }) };
    Check(fireCall(valid).Succeeded() && net.calls.size() == 1U, "a valid call did not reach the session");
    const FakeNet::Call& sent = net.calls.front();
    Check(sent.name == "Fire" && sent.target == 0U && sent.args.size() == 6U && std::get<std::uint64_t>(sent.args[0]) == 42U && std::get<float>(sent.args[1]) == 0.5F &&
            std::get<float>(sent.args[2]) == -1.0F && std::get<std::string>(sent.args[3]) == "hi" && std::get<bool>(sent.args[4]) && std::get<std::int32_t>(sent.args[5]) == 7,
        "the arguments were not mapped to the declaration");
    const auto with = [&](std::vector<ScriptFunctionArgument> base, std::vector<ScriptFunctionArgument> changes) {
        for (ScriptFunctionArgument& change : changes) {
            bool replaced = false;
            for (ScriptFunctionArgument& argument : base) {
                if (argument.name == change.name) {
                    argument = change;
                    replaced = true;
                }
            }
            if (!replaced) {
                base.push_back(change);
            }
        }
        return base;
    };
    const std::size_t sentBefore = net.calls.size();
    Check(Failed(fireCall(with(valid, { Arg("n1", ScriptValue{ 2.0 }) })), "outside"), "a float above its range was sent");
    Check(Failed(fireCall(with(valid, { Arg("n3", ScriptValue{ 10.0 }) })), "outside"), "an int above its range was sent");
    Check(Failed(fireCall(with(valid, { Arg("n3", ScriptValue{ 2.5 }) })), "whole number"), "a fractional int was sent");
    Check(Failed(fireCall(with(valid, { Arg("s1", ScriptValue{ std::string(17U, 'x') }) })), "longer than"), "a long string was sent");
    Check(Failed(fireCall(with(valid, { Arg("e1", ScriptValue{ std::uint64_t{ 0U }, kb::script::ScriptValueType::Entity }) })), "not an entity"), "a null entity was sent");
    Check(Failed(fireCall(with(valid, { Arg("s2", ScriptValue{ std::string{ "extra" } }) })), "does not take"), "an argument the RPC does not have was sent");
    Check(Failed(fireCall({ Arg("e1", ScriptValue{ std::uint64_t{ 1U }, kb::script::ScriptValueType::Entity }) }), "missing argument n1"), "a missing argument was sent");
    Check(Failed(CallNet(host, scene, "Net.Call", { Arg("rpc", ScriptValue{ std::string{ "Nope" } }) }), "no RPC named"), "an unknown RPC was called");
    Check(!CallNet(host, scene, "Net.Call", { Arg("rpc", ScriptValue{ 5 }) }).Succeeded(), "a non-string RPC name was accepted");
    Check(net.calls.size() == sentBefore, "a refused call reached the session");
    // Pin types are checked by the function registry as for every API.
    Check(!CallNet(host, scene, "Net.Call", { Arg("rpc", ScriptValue{ std::string{ "Fire" } }), Arg("n1", ScriptValue{ std::string{ "text" } }) }).Succeeded(), "a string in a number slot was accepted");

    // Server calls with a target.
    net.mode = ScriptNetMode::Server;
    Check(CallNet(host, scene, "Net.Call", { Arg("rpc", ScriptValue{ std::string{ "Announce" } }), Arg("s1", ScriptValue{ std::string{ "welcome" } }), Arg("target", ScriptValue{ std::uint32_t{ 9U } }) }).Succeeded() &&
            net.calls.back().target == 9U,
        "a targeted server call did not carry its target");

    // Replication.
    const ScriptValue entity{ std::uint64_t{ 77U }, kb::script::ScriptValueType::Entity };
    Check(CallNet(host, scene, "Net.Replicate", { Arg("entity", entity) }).Succeeded() && CallNet(host, scene, "Net.IsReplicated", { Arg("entity", entity) }).Output("value")->AsBool(),
        "an entity could not be marked as replicated");
    Check(!CallNet(host, scene, "Net.IsReplicated", { Arg("entity", ScriptValue{ std::uint64_t{ 5U }, kb::script::ScriptValueType::Entity }) }).Output("value")->AsBool(), "an unmarked entity is replicated");
    Check(CallNet(host, scene, "Net.SetOwner", { Arg("entity", entity), Arg("player", ScriptValue{ std::uint32_t{ 3U } }) }).Succeeded() && net.owners[77U] == 3U, "ownership did not reach the session");
    net.mode = ScriptNetMode::Client;
    Check(Failed(CallNet(host, scene, "Net.Replicate", { Arg("entity", entity) }), "only a server") && Failed(CallNet(host, scene, "Net.SetOwner", { Arg("entity", entity), Arg("player", ScriptValue{ std::uint32_t{ 3U } }) }), "only a server"),
        "a client replicated or gave away an entity");

    ScriptNetApi::Attach(scene, nullptr);
    Check(ScriptNetApi::Attached(scene) == nullptr, "the session was not detached");
}

constexpr const char* kNetScript = R"lua(
function Ready(self, dt)
    local registered = CallFunction("Net.RegisterRpc", { name = "Shout", direction = "toServer", args = "string(8),float(0..10)" })
    SetShared("lua.registered", registered ~= nil and registered == true)
    local mode = CallFunction("Net.Mode", {})
    SetShared("lua.mode", mode.mode)
    SetShared("lua.player", CallFunction("Net.LocalPlayerId", {}))
    local sent = CallFunction("Net.Call", { rpc = "Shout", s1 = "hey", n1 = 2.5 })
    SetShared("lua.sent", sent == true)
    local failed, why = CallFunction("Net.Call", { rpc = "Shout", s1 = "far too long a shout", n1 = 2.5 })
    SetShared("lua.refused", failed == nil and why ~= nil)
end

function OnNetConnected(self, event)
    SetShared("lua.connected", event.args.player)
end

function OnNetDisconnected(self, event)
    SetShared("lua.disconnected", event.args.player)
    SetShared("lua.reason", event.args.reason)
end

function OnRpc_Hit(self, event)
    SetShared("lua.hit.sender", event.args.sender)
    SetShared("lua.hit.damage", event.args.n1)
    SetShared("lua.hit.who", event.args.s1)
end
)lua";

void RunLuaAndEventsTest() {
    // The script is a real asset of a project, so the installed script scene system can prepare and run it.
    const std::filesystem::path projectRoot = std::filesystem::temp_directory_path() / "21kb_script_net_api_test";
    std::error_code fileError;
    std::filesystem::remove_all(projectRoot, fileError);
    std::filesystem::create_directories(projectRoot / "Assets" / "Logic", fileError);
    {
        std::ofstream script(projectRoot / "Assets" / "Logic" / "NetScript.lua", std::ios::binary | std::ios::trunc);
        script << kNetScript;
    }
    kb::scene::Scene scene;
    Check(scene.Assets().MountProject(projectRoot) && scene.Assets().Discover() == 1U, "the Net test project could not be mounted");
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/NetScript.lua");
    Check(metadata != nullptr, "the Net test script is not in the project");
    kb::script::ScriptRuntimeHost host{ scene };
    FakeNet net;
    net.mode = ScriptNetMode::Client;
    net.player = 5U;
    ScriptNetApi::Attach(scene, &net);
    const kb::assets::AssetId kAsset = metadata->id;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "NetScript" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = kAsset.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
    Check(host.InstallSceneSystem(), "the script scene system was not installed");

    // The first frame runs Created and Ready.
    static_cast<void>(scene.Runtime().Update(0.016F));
    const auto shared = [&host](const char* key) { return host.SharedState().Get(key); };
    Check(shared("lua.registered") && shared("lua.registered")->AsBool(), "a script could not register an RPC");
    Check(shared("lua.mode") && shared("lua.mode")->AsString() == "client" && shared("lua.player") && shared("lua.player")->AsInt() == 5, "a script cannot read the session");
    Check(shared("lua.sent") && shared("lua.sent")->AsBool() && net.calls.size() == 1U && net.calls[0].name == "Shout" && std::get<std::string>(net.calls[0].args[0]) == "hey" &&
            std::get<float>(net.calls[0].args[1]) == 2.5F,
        "a script call did not reach the session with its arguments");
    Check(shared("lua.refused") && shared("lua.refused")->AsBool(), "a script call with a bad argument was not refused");

    // Events from the session reach the behaviours in the next frame.
    ScriptNetApi::QueueEvent(scene, kb::script::ScriptNetEvent{ "OnNetConnected", { { "player", std::uint32_t{ 12U } } } });
    ScriptNetApi::QueueEvent(scene, kb::script::ScriptNetEvent{ "OnRpc_Hit", { { "sender", std::uint32_t{ 4U } }, { "n1", 33.5F }, { "s1", std::string{ "goblin" } } } });
    ScriptNetApi::QueueEvent(scene, kb::script::ScriptNetEvent{ "OnNetDisconnected", { { "player", std::uint32_t{ 12U } }, { "reason", std::string{ "timeout" } } } });
    static_cast<void>(scene.Runtime().Update(0.016F));
    Check(shared("lua.connected") && shared("lua.connected")->AsInt() == 12, "OnNetConnected did not reach the script");
    Check(shared("lua.disconnected") && shared("lua.disconnected")->AsInt() == 12 && shared("lua.reason")->AsString() == "timeout", "OnNetDisconnected did not reach the script");
    Check(shared("lua.hit.sender") && shared("lua.hit.sender")->AsInt() == 4 && shared("lua.hit.damage")->AsFloat() == 33.5F && shared("lua.hit.who")->AsString() == "goblin",
        "an RPC event did not reach the script with its arguments");

    // The queue is bounded: a flood keeps the newest events and costs bounded memory.
    for (int index = 0; index < 5000; ++index) {
        ScriptNetApi::QueueEvent(scene, kb::script::ScriptNetEvent{ "OnNetConnected", { { "player", std::uint32_t{ static_cast<std::uint32_t>(index) } } } });
    }
    const std::vector<kb::script::ScriptNetEvent> drained = ScriptNetApi::DrainEvents(scene);
    Check(drained.size() == 1024U && std::get<std::uint32_t>(drained.back().arguments.front().second) == 4999U, "the event queue is not bounded to the newest events");
    ScriptNetApi::Attach(scene, nullptr);
}

} // namespace

namespace kb::tests {

void RunScriptNetApiTests() {
    RunSpecParserTest();
    RunOfflineTest();
    RunDeclareAndCallTest();
    RunLuaAndEventsTest();
}

} // namespace kb::tests
