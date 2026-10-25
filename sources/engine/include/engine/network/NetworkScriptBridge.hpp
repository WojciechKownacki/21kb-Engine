#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <variant>
#include <vector>

// The seam between the script API (Net.*, engine/script/ScriptNetApi.hpp) and a network layer. The engine
// does not link a network library: a game or a dedicated server attaches an implementation of
// INetworkScriptRuntime to a scene (ScriptNetApi::Attach), and scripts see Net.* work. Without one, the
// scene is offline and every Net.* function that needs a session says so.
namespace kb::network {

enum class ScriptNetMode : std::uint8_t { Offline, Server, Client };

enum class ScriptRpcArgKind : std::uint8_t { Bool, Int, UInt, Float, String, Entity };

struct ScriptRpcArgSpec {
    ScriptRpcArgKind kind = ScriptRpcArgKind::Bool;
    double minimum = std::numeric_limits<double>::lowest(); // Int, UInt, Float
    double maximum = std::numeric_limits<double>::max();
    std::uint16_t maxLength = 64U; // String
};

struct ScriptRpcDeclaration {
    std::string name;
    bool toServer = true; // false: the server calls the clients
    bool reliable = true;
    std::vector<ScriptRpcArgSpec> args;
    int ownerArg = -1; // index of an Entity argument the caller must own (client calls only), or -1
    std::uint32_t maxCallsPerSecond = 30U;
};

// Entity values are the scene entity ids as std::uint64_t.
using ScriptRpcValue = std::variant<bool, std::int32_t, std::uint32_t, float, std::string, std::uint64_t>;

class INetworkScriptRuntime {
public:
    INetworkScriptRuntime() = default;
    INetworkScriptRuntime(const INetworkScriptRuntime&) = delete;
    INetworkScriptRuntime& operator=(const INetworkScriptRuntime&) = delete;
    virtual ~INetworkScriptRuntime() = default;

    [[nodiscard]] virtual ScriptNetMode Mode() const = 0;
    [[nodiscard]] virtual bool Connected() const = 0;
    // The client id of this machine's player; 0 on a server.
    [[nodiscard]] virtual std::uint64_t LocalPlayerId() const = 0;
    [[nodiscard]] virtual std::size_t PlayerCount() const = 0;

    // Declares an RPC. Only possible before the session starts; false with `error` after that, for a repeated
    // name or for a declaration the network layer cannot carry.
    virtual bool DeclareRpc(const ScriptRpcDeclaration& declaration, std::string& error) = 0;
    [[nodiscard]] virtual const ScriptRpcDeclaration* FindRpc(const std::string& name) const = 0;
    // `target`: on a server the client id to call, 0 for every client; on a client it must be 0 (the server).
    virtual bool CallRpc(const std::string& name, std::uint64_t target, std::span<const ScriptRpcValue> args, std::string& error) = 0;

    // Server only.
    virtual bool Replicate(std::uint64_t entity, std::string& error) = 0;
    [[nodiscard]] virtual bool IsReplicated(std::uint64_t entity) const = 0;
    virtual bool SetOwner(std::uint64_t entity, std::uint64_t player, std::string& error) = 0;
};

} // namespace kb::network
