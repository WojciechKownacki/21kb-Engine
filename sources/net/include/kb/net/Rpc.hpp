#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Remote procedure calls with declared signatures. Both ends build the same RpcRegistry (the same
// definitions in the same order); a call travels as an id plus typed values and the receiver checks
// the direction, the value types and ranges, the size, the caller's right to act on the entity it names
// and how often it calls, before any handler runs. A call that fails a check is dropped and counted;
// it never reaches a handler and never throws out of the receiving code.
namespace kb::net {

using NetworkId = std::uint32_t; // 0 is never a valid network id

enum class RpcArgType : std::uint8_t { Bool, Int32, UInt32, Float, String, NetworkId };

struct RpcArgSpec {
    RpcArgType type = RpcArgType::Bool;
    // Inclusive range for Int32, UInt32 and Float (a Float must also be finite).
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();
    std::uint16_t maxLength = 64U; // String: most bytes
};

enum class RpcDirection : std::uint8_t { ClientToServer, ServerToClient };

struct RpcDefinition {
    std::string name;
    RpcDirection direction = RpcDirection::ClientToServer;
    bool reliable = true; // reliable ordered or unreliable sequenced channel
    std::vector<RpcArgSpec> args;
    // Index of a NetworkId argument naming an entity the caller must own, or -1. Only for ClientToServer.
    int ownerArg = -1;
    std::uint32_t maxCallsPerSecond = 30U; // per caller; a burst of this many is allowed
};

// NetworkId values are carried as std::uint32_t.
using RpcValue = std::variant<bool, std::int32_t, std::uint32_t, float, std::string>;

inline constexpr std::size_t kMaxRpcArgs = 8U;
inline constexpr std::size_t kMaxRpcPayloadBytes = 900U;

enum class RpcStatus : std::uint8_t {
    Ok,
    UnknownRpc,
    WrongDirection,
    BadArguments, // wrong count, type, range or length
    Malformed,    // the bytes do not parse
    TooLarge,
    Busy,         // the send queue is full; try again later (only from the calling side)
    NotReady,     // the connection is not ready for calls yet (only from the calling side)
};

class RpcRegistry final {
public:
    // False, with `error` set, when the definition is not acceptable: empty or repeated name, too many
    // arguments, an inverted range, or an ownerArg that is not a NetworkId argument of a ClientToServer call.
    [[nodiscard]] bool Define(RpcDefinition definition, std::string& error);
    // Id 0 is never used; ids are 1-based in definition order.
    [[nodiscard]] const RpcDefinition* Find(std::uint16_t id) const noexcept;
    [[nodiscard]] const RpcDefinition* FindByName(const std::string& name, std::uint16_t& id) const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept { return definitions_.size(); }
    // Equal on both ends if and only if the definitions are the same; sent in the hello so that a
    // client built against other definitions is refused up front.
    [[nodiscard]] std::uint64_t Fingerprint() const noexcept;

private:
    [[nodiscard]] bool FindByNameUnchecked(const std::string& name) const noexcept;

    std::vector<RpcDefinition> definitions_;
};

[[nodiscard]] RpcStatus ValidateRpcArguments(const RpcDefinition& definition, std::span<const RpcValue> args);
[[nodiscard]] RpcStatus EncodeRpc(const RpcRegistry& registry, std::uint16_t id, std::span<const RpcValue> args, std::vector<std::uint8_t>& out);
// Parses and validates a call that arrived with `expected` direction. On Ok, `id` and `args` are set.
[[nodiscard]] RpcStatus DecodeRpc(const RpcRegistry& registry, std::span<const std::uint8_t> payload, RpcDirection expected, std::uint16_t& id,
    std::vector<RpcValue>& args);

// Token bucket per (caller, rpc): `perSecond` calls per second with a burst of `perSecond`.
class RpcRateLimiter final {
public:
    [[nodiscard]] bool Allow(std::uint64_t caller, std::uint16_t rpc, std::uint32_t perSecond, std::uint64_t nowMilliseconds);
    void Forget(std::uint64_t caller);

private:
    struct Bucket {
        double tokens = 0.0;
        std::uint64_t lastMilliseconds = 0U;
        bool initialised = false;
    };
    std::map<std::pair<std::uint64_t, std::uint16_t>, Bucket> buckets_;
};

} // namespace kb::net
