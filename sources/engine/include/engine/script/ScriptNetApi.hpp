#pragma once

#include "engine/network/NetworkScriptBridge.hpp"

#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::script {

class ScriptRuntimeHost;

// A network event for the scripts of a scene: OnNetConnected(player), OnNetDisconnected(player, reason) and
// OnRpc_<Name>(sender, then the call's arguments as n1.. s1.. b1.. e1..), delivered to every behaviour as a
// world event in the next script frame.
struct ScriptNetEvent {
    std::string name;
    std::vector<std::pair<std::string, kb::network::ScriptRpcValue>> arguments;
};

// Net.* for scripts and Visual Graph. The functions work on the INetworkScriptRuntime attached to the
// calling scene; with none attached the scene is offline.
class ScriptNetApi final {
public:
    [[nodiscard]] static bool Register(ScriptRuntimeHost& host);

    // Attaches (nullptr detaches) a runtime to a scene. The runtime must outlive the attachment.
    static void Attach(kb::scene::Scene& scene, kb::network::INetworkScriptRuntime* runtime);
    [[nodiscard]] static kb::network::INetworkScriptRuntime* Attached(const kb::scene::Scene& scene);

    // Queues an event for the scene's scripts (at most 1024 wait at once; the oldest is dropped beyond that).
    static void QueueEvent(const kb::scene::Scene& scene, ScriptNetEvent event);
    [[nodiscard]] static std::vector<ScriptNetEvent> DrainEvents(const kb::scene::Scene& scene);

    // Parses "float(-1..1),string(32),entity,bool" into argument specs; false with `error` set when it is not valid.
    [[nodiscard]] static bool ParseArgumentSpecs(const std::string& text, std::vector<kb::network::ScriptRpcArgSpec>& specs, std::string& error);
};

} // namespace kb::script
