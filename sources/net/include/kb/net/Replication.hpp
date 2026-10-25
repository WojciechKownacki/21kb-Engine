#pragma once

#include "engine/ecs/World.hpp"
#include "engine/network/ReplicationSchema.hpp"
#include "kb/net/Host.hpp"
#include "kb/net/Rpc.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

// State replication and RPC on top of Host and Client.
//
// The server is authoritative. Entities it marks as replicated get a network id (never reused in a
// run) and are sent to each client that is relevant to them: once, in full, in a reliable Spawn, then
// as unreliable delta updates. Only the components registered in the ReplicationRegistry travel, each
// through a codec that quantizes its fields. A delta is computed against what the client has
// acknowledged, and an update carries everything that differs from that baseline, so a lost or
// reordered update is repaired by the next one and the client converges without any retransmission
// of state. Spawn, Despawn and ownership changes are reliable and ordered.
//
// Why not the world snapshot of the ECS (WorldSnapshot.hpp, chunked snapshots and deltas): it is made
// to save and restore a whole world on one machine (raw component bytes in memory layout, local
// entity ids, every component of every entity, no per-client view), which is the wrong unit for a
// network: it cannot be limited to replicated components or to what a client may see, cannot be
// quantized, and carries pointers-sized and padded layouts that differ between builds.
//
// Messages travel on channel 0 (reliable ordered) and channel 1 (unreliable sequenced) of the
// session, which is what the default SessionConfig provides.
namespace kb::net {

inline constexpr std::size_t kMaxReplicatedComponents = 16U;

// Turns one component type into a fixed-size byte string and back. The byte string is also what the
// server compares to decide whether a component changed, so values that differ by less than the
// quantization step never cost a byte.
class ComponentCodec {
public:
    ComponentCodec() = default;
    ComponentCodec(const ComponentCodec&) = delete;
    ComponentCodec& operator=(const ComponentCodec&) = delete;
    virtual ~ComponentCodec() = default;

    [[nodiscard]] virtual const std::string& Name() const noexcept = 0;
    [[nodiscard]] virtual bool Valid() const noexcept = 0;
    [[nodiscard]] virtual std::size_t EncodedSize() const noexcept = 0;
    // Describes the layout; goes into the fingerprint so that two builds that encode differently refuse each other.
    [[nodiscard]] virtual std::string Describe() const = 0;
    // False when the entity does not have the component. `out` has EncodedSize() bytes.
    [[nodiscard]] virtual bool Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const = 0;
    // True when `bytes` (EncodedSize() of them) holds only values inside the declared ranges.
    [[nodiscard]] virtual bool Validate(std::span<const std::uint8_t> bytes) const = 0;
    // Sets the component from validated bytes.
    virtual void Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const = 0;
    virtual void Remove(kb::ecs::World& world, kb::ecs::Entity entity) const = 0;
};

// A codec for a struct of float, unsigned and bool members. Floats are quantized to `bits` (1 to 24)
// over [minimum, maximum]; a value outside the range is clamped.
template <typename T>
class StructCodec final : public ComponentCodec {
public:
    explicit StructCodec(std::string name) : name_(std::move(name)) {}

    StructCodec& Float(float T::*member, float minimum, float maximum, unsigned bits) {
        kb::network::ReplicatedField spec;
        spec.id = 1U;
        spec.name = "f";
        spec.type = kb::network::ReplicatedFieldType::QuantizedFloat;
        spec.minimum = minimum;
        spec.maximum = maximum;
        spec.quantizationBits = static_cast<std::uint8_t>(bits);
        if (bits == 0U || bits > 24U || !kb::network::QuantizeFloat(spec, minimum).has_value() || !kb::network::QuantizeFloat(spec, maximum).has_value()) {
            valid_ = false;
            return *this;
        }
        const std::size_t bytes = (bits + 7U) / 8U;
        Field field;
        field.bytes = bytes;
        field.description = "f" + std::to_string(bits) + ":" + std::to_string(minimum) + ":" + std::to_string(maximum);
        field.write = [member, spec, bytes](const T& value, std::uint8_t* out) {
            float v = value.*member;
            v = std::isnan(v) ? spec.minimum : std::clamp(v, spec.minimum, spec.maximum);
            const std::uint32_t q = kb::network::QuantizeFloat(spec, v).value_or(0U);
            for (std::size_t i = 0U; i < bytes; ++i) {
                out[i] = static_cast<std::uint8_t>((q >> (8U * i)) & 0xFFU);
            }
        };
        field.read = [member, spec, bytes](T& value, const std::uint8_t* in) {
            std::uint32_t q = 0U;
            for (std::size_t i = 0U; i < bytes; ++i) {
                q |= static_cast<std::uint32_t>(in[i]) << (8U * i);
            }
            const std::optional<float> v = kb::network::DequantizeFloat(spec, q);
            if (!v.has_value()) {
                return false;
            }
            value.*member = *v;
            return true;
        };
        Add(std::move(field));
        return *this;
    }

    StructCodec& UInt(std::uint32_t T::*member, std::uint32_t maximum) {
        const std::size_t bytes = maximum <= 0xFFU ? 1U : maximum <= 0xFFFFU ? 2U : maximum <= 0xFFFFFFU ? 3U : 4U;
        Field field;
        field.bytes = bytes;
        field.description = "u" + std::to_string(maximum);
        field.write = [member, maximum, bytes](const T& value, std::uint8_t* out) {
            const std::uint32_t v = std::min(value.*member, maximum);
            for (std::size_t i = 0U; i < bytes; ++i) {
                out[i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
            }
        };
        field.read = [member, maximum, bytes](T& value, const std::uint8_t* in) {
            std::uint32_t v = 0U;
            for (std::size_t i = 0U; i < bytes; ++i) {
                v |= static_cast<std::uint32_t>(in[i]) << (8U * i);
            }
            if (v > maximum) {
                return false;
            }
            value.*member = v;
            return true;
        };
        Add(std::move(field));
        return *this;
    }

    StructCodec& Bool(bool T::*member) {
        Field field;
        field.bytes = 1U;
        field.description = "b";
        field.write = [member](const T& value, std::uint8_t* out) { out[0] = value.*member ? 1U : 0U; };
        field.read = [member](T& value, const std::uint8_t* in) {
            if (in[0] > 1U) {
                return false;
            }
            value.*member = in[0] == 1U;
            return true;
        };
        Add(std::move(field));
        return *this;
    }

    [[nodiscard]] const std::string& Name() const noexcept override { return name_; }
    [[nodiscard]] bool Valid() const noexcept override { return valid_ && size_ != 0U; }
    [[nodiscard]] std::size_t EncodedSize() const noexcept override { return size_; }
    [[nodiscard]] std::string Describe() const override {
        std::string text = name_;
        for (const Field& field : fields_) {
            text += "|" + field.description;
        }
        return text;
    }

    [[nodiscard]] bool Capture(const kb::ecs::World& world, kb::ecs::Entity entity, std::span<std::uint8_t> out) const override {
        const T* component = world.TryGet<T>(entity);
        if (component == nullptr || out.size() != size_) {
            return false;
        }
        std::size_t offset = 0U;
        for (const Field& field : fields_) {
            field.write(*component, out.data() + offset);
            offset += field.bytes;
        }
        return true;
    }

    [[nodiscard]] bool Validate(std::span<const std::uint8_t> bytes) const override {
        if (bytes.size() != size_) {
            return false;
        }
        T scratch{};
        std::size_t offset = 0U;
        for (const Field& field : fields_) {
            if (!field.read(scratch, bytes.data() + offset)) {
                return false;
            }
            offset += field.bytes;
        }
        return true;
    }

    void Apply(kb::ecs::World& world, kb::ecs::Entity entity, std::span<const std::uint8_t> bytes) const override {
        const T* existing = world.TryGet<T>(entity);
        T value = existing != nullptr ? *existing : T{};
        std::size_t offset = 0U;
        for (const Field& field : fields_) {
            if (!field.read(value, bytes.data() + offset)) {
                return;
            }
            offset += field.bytes;
        }
        world.Set(entity, value);
    }

    void Remove(kb::ecs::World& world, kb::ecs::Entity entity) const override {
        if (world.Has<T>(entity)) {
            world.Remove<T>(entity);
        }
    }

private:
    struct Field {
        std::size_t bytes = 0U;
        std::string description;
        std::function<void(const T&, std::uint8_t*)> write;
        std::function<bool(T&, const std::uint8_t*)> read;
    };

    void Add(Field field) {
        size_ += field.bytes;
        fields_.push_back(std::move(field));
    }

    std::string name_;
    std::vector<Field> fields_;
    std::size_t size_ = 0U;
    bool valid_ = true;
};

class ReplicationRegistry final {
public:
    // False when the codec is invalid, has a repeated name, or the registry is full.
    [[nodiscard]] bool Add(std::unique_ptr<ComponentCodec> codec, std::string& error);
    [[nodiscard]] std::size_t Count() const noexcept { return codecs_.size(); }
    [[nodiscard]] const ComponentCodec& At(std::size_t index) const { return *codecs_[index]; }
    [[nodiscard]] std::uint64_t Fingerprint() const noexcept;

private:
    std::vector<std::unique_ptr<ComponentCodec>> codecs_;
};

// Decides which replicated entities a client may see. Called every tick for each pair.
class IRelevance {
public:
    IRelevance() = default;
    IRelevance(const IRelevance&) = delete;
    IRelevance& operator=(const IRelevance&) = delete;
    virtual ~IRelevance() = default;
    [[nodiscard]] virtual bool IsRelevant(std::uint64_t clientId, NetworkId id, kb::ecs::Entity entity) = 0;
};

// Relevance by distance in the plane. An entity becomes relevant inside `radius` and stops being
// relevant outside `radius * hysteresis`, so one standing on the edge does not flicker.
class RadiusRelevance final : public IRelevance {
public:
    using ViewerPosition = std::function<bool(std::uint64_t clientId, float& x, float& y)>;
    using EntityPosition = std::function<bool(kb::ecs::Entity entity, float& x, float& y)>;

    RadiusRelevance(float radius, float hysteresis, ViewerPosition viewer, EntityPosition entity)
        : radius_(radius), hysteresis_(std::max(hysteresis, 1.0F)), viewer_(std::move(viewer)), entity_(std::move(entity)) {}

    [[nodiscard]] bool IsRelevant(std::uint64_t clientId, NetworkId id, kb::ecs::Entity entity) override;

private:
    float radius_;
    float hysteresis_;
    ViewerPosition viewer_;
    EntityPosition entity_;
    std::set<std::pair<std::uint64_t, NetworkId>> inside_;
};

struct ReplicationConfig {
    std::size_t maxEntities = 4096U;           // replicated entities on the server, replicas on a client
    std::size_t maxUpdateBytes = 900U;         // largest state message
    std::size_t maxBytesPerClientPerTick = 8000U;
    std::size_t maxUpdatesInFlight = 64U;      // sent but not yet acknowledged state messages, per client
};

struct ReplicationServerStats {
    std::uint64_t spawnsSent = 0U;
    std::uint64_t despawnsSent = 0U;
    std::uint64_t stateMessagesSent = 0U;
    std::uint64_t entriesSent = 0U;
    std::uint64_t acksReceived = 0U;
    std::uint64_t sendBusy = 0U;
    std::uint64_t rejectedMalformed = 0U;
    std::uint64_t rejectedNotReady = 0U;
    std::uint64_t helloMismatch = 0U;
    std::uint64_t rpcReceived = 0U;
    std::uint64_t rpcRejectedMalformed = 0U;
    std::uint64_t rpcRejectedDirection = 0U;
    std::uint64_t rpcRejectedArguments = 0U;
    std::uint64_t rpcRejectedPermission = 0U;
    std::uint64_t rpcRejectedRate = 0U;
    std::uint64_t rpcNoHandler = 0U;
    std::uint64_t rpcHandlerErrors = 0U;
    std::uint64_t rpcDispatched = 0U;
};

class ReplicationServer final {
public:
    using RpcHandler = std::function<void(std::uint64_t caller, std::span<const RpcValue> args)>;

    ReplicationServer(Host& host, kb::ecs::World& world, const ReplicationRegistry& components, const RpcRegistry& rpcs, ReplicationConfig config = {});

    // Marks an entity as replicated. Returns 0 when the entity is not alive, is already replicated or the limit is reached.
    [[nodiscard]] NetworkId Replicate(kb::ecs::Entity entity, std::uint64_t owner = 0U);
    // Stops replicating; clients that have it receive a Despawn. The entity itself is not touched.
    bool Unreplicate(NetworkId id);
    bool SetOwner(NetworkId id, std::uint64_t owner);
    [[nodiscard]] kb::ecs::Entity EntityOf(NetworkId id) const;
    [[nodiscard]] std::uint64_t OwnerOf(NetworkId id) const;
    [[nodiscard]] std::size_t ReplicatedCount() const noexcept { return entities_.size(); }
    void SetRelevance(IRelevance* relevance) noexcept { relevance_ = relevance; }

    // Feed every NetEvent the host produced. Connected starts the hello; Message is checked and dispatched;
    // Disconnected forgets the client.
    void HandleEvent(const NetEvent& event, std::uint64_t nowMilliseconds);
    // The same check for one message, for callers that read events themselves and for tests.
    void HandleMessage(std::uint64_t client, std::uint8_t channel, std::span<const std::uint8_t> data, std::uint64_t nowMilliseconds);
    // Builds and sends the state updates for this tick. `tick` must increase from call to call.
    void Tick(std::uint32_t tick, std::uint64_t nowMilliseconds);

    // RPC to one client, or to every ready client.
    [[nodiscard]] RpcStatus CallRpc(std::uint64_t client, const std::string& name, std::span<const RpcValue> args);
    void CallRpcAll(const std::string& name, std::span<const RpcValue> args, RpcStatus& status);
    void SetRpcHandler(const std::string& name, RpcHandler handler);

    [[nodiscard]] const ReplicationServerStats& Stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t ReadyClients() const noexcept;
    [[nodiscard]] std::size_t VisibleCount(std::uint64_t client) const;

private:
    struct Baseline {
        std::uint32_t epoch = 0U; // which Spawn of this entity the baseline belongs to
        std::uint32_t tick = 0U;
        std::uint16_t presentMask = 0U;
        std::array<std::vector<std::uint8_t>, kMaxReplicatedComponents> bytes;
    };
    struct SentEntry {
        NetworkId id = 0U;
        std::uint32_t epoch = 0U;
        std::uint32_t tick = 0U;
        std::uint16_t changedMask = 0U;
        std::uint16_t removedMask = 0U;
        std::array<std::vector<std::uint8_t>, kMaxReplicatedComponents> bytes;
    };
    struct Replicated {
        kb::ecs::Entity entity;
        std::uint64_t owner = 0U;
        std::uint16_t presentMask = 0U;
        std::array<std::vector<std::uint8_t>, kMaxReplicatedComponents> current;
    };
    struct ClientState {
        bool ready = false;
        std::set<NetworkId> visible;
        std::map<NetworkId, Baseline> baseline;
        std::map<std::uint32_t, std::vector<SentEntry>> inFlight;
        std::uint32_t nextUpdateId = 1U;
        NetworkId cursor = 0U;
        std::uint32_t nextEpoch = 1U;
    };

    void Capture(Replicated& replicated);
    [[nodiscard]] bool SendSpawn(std::uint64_t client, ClientState& state, NetworkId id, const Replicated& replicated, std::uint32_t tick);
    [[nodiscard]] bool SendReliable(std::uint64_t client, std::span<const std::uint8_t> message);
    void Despawn(std::uint64_t client, ClientState& state, NetworkId id);
    void UpdateVisibility(std::uint64_t client, ClientState& state, std::uint32_t tick);
    void BuildUpdates(std::uint64_t client, ClientState& state, std::uint32_t tick);
    void HandleAck(ClientState& state, std::span<const std::uint8_t> data);
    void ApplyAcked(ClientState& state, const std::vector<SentEntry>& entries);
    void HandleRpc(std::uint64_t client, std::uint8_t channel, bool reliable, std::span<const std::uint8_t> data, std::uint64_t nowMilliseconds);
    [[nodiscard]] RpcStatus SendRpc(std::uint64_t client, std::uint16_t id, const RpcDefinition& definition, std::span<const RpcValue> args);

    Host* host_;
    kb::ecs::World* world_;
    const ReplicationRegistry* components_;
    const RpcRegistry* rpcs_;
    ReplicationConfig config_;
    IRelevance* relevance_ = nullptr;
    std::map<NetworkId, Replicated> entities_;
    std::map<kb::ecs::Entity::IdType, NetworkId> byEntity_;
    NetworkId nextId_ = 1U;
    std::map<std::uint64_t, ClientState> clients_;
    std::map<std::uint16_t, RpcHandler> handlers_;
    RpcRateLimiter limiter_;
    ReplicationServerStats stats_;
    std::uint32_t lastTick_ = 0U;
};

struct ReplicationClientStats {
    std::uint64_t spawnsApplied = 0U;
    std::uint64_t despawnsApplied = 0U;
    std::uint64_t stateMessagesApplied = 0U;
    std::uint64_t entriesApplied = 0U;
    std::uint64_t entriesStale = 0U;
    std::uint64_t entriesUnknownEntity = 0U;
    std::uint64_t ownerChanges = 0U;
    std::uint64_t acksSent = 0U;
    std::uint64_t rejectedMalformed = 0U;
    std::uint64_t rejectedNotReady = 0U;
    std::uint64_t rejectedLimit = 0U;
    std::uint64_t rpcReceived = 0U;
    std::uint64_t rpcRejected = 0U;
    std::uint64_t rpcNoHandler = 0U;
    std::uint64_t rpcHandlerErrors = 0U;
    std::uint64_t rpcDispatched = 0U;
    std::uint64_t helloMismatch = 0U;
};

class ReplicationClient final {
public:
    using RpcHandler = std::function<void(std::span<const RpcValue> args)>;

    ReplicationClient(Client& client, kb::ecs::World& world, const ReplicationRegistry& components, const RpcRegistry& rpcs, ReplicationConfig config = {});

    void HandleEvent(const NetEvent& event);
    void HandleMessage(std::uint8_t channel, std::span<const std::uint8_t> data);
    // Sends the acknowledgement of the state messages received since the last call. Call it once per tick.
    void Flush();

    [[nodiscard]] kb::ecs::Entity Find(NetworkId id) const;
    [[nodiscard]] std::uint64_t OwnerOf(NetworkId id) const;
    [[nodiscard]] std::size_t ReplicaCount() const noexcept { return replicas_.size(); }
    [[nodiscard]] std::vector<NetworkId> ReplicaIds() const;
    [[nodiscard]] bool Ready() const noexcept { return ready_; }

    [[nodiscard]] RpcStatus CallRpc(const std::string& name, std::span<const RpcValue> args);
    void SetRpcHandler(const std::string& name, RpcHandler handler);

    [[nodiscard]] const ReplicationClientStats& Stats() const noexcept { return stats_; }

private:
    struct Replica {
        kb::ecs::Entity entity;
        std::uint64_t owner = 0U;
        std::uint32_t lastTick = 0U;
    };

    void Reset();
    void HandleSpawn(std::span<const std::uint8_t> data);
    void HandleDespawn(std::span<const std::uint8_t> data);
    void HandleOwner(std::span<const std::uint8_t> data);
    void HandleState(std::span<const std::uint8_t> data);
    void HandleHello(std::span<const std::uint8_t> data);
    void HandleRpc(std::span<const std::uint8_t> data);

    Client* client_;
    kb::ecs::World* world_;
    const ReplicationRegistry* components_;
    const RpcRegistry* rpcs_;
    ReplicationConfig config_;
    std::map<NetworkId, Replica> replicas_;
    std::map<std::uint16_t, RpcHandler> handlers_;
    ReplicationClientStats stats_;
    bool ready_ = false;
    std::uint32_t latestUpdate_ = 0U;
    std::uint32_t receivedBits_ = 0U; // bit i: update (latestUpdate_ - 1 - i) was received
    bool ackDirty_ = false;
    bool helloAckPending_ = false;
};

} // namespace kb::net
