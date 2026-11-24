// Tests for the replication and RPC layer of kb_net: codecs, RPC validation, state convergence on a
// perfect and on a bad network, permissions, relevance, hostile messages.

#include "kb/net/Prediction.hpp"
#include "kb/net/ByteIO.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/CombatantComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/net/ProjectScene.hpp"
#include <fstream>
#include <filesystem>
#include "engine/ecs/QueryFilter.hpp"
#include "kb/net/Replication.hpp"
#if defined(KB_NET_WEBRTC)
#include "kb/net/WebRtcTransport.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace kb::net;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Position {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};
struct Health {
    std::uint32_t hp = 100U;
    bool alive = true;
};
struct Facing {
    float yaw = 0.0F;
};
struct Secret {
    int value = 0; // never registered for replication
};
struct InputAck {
    std::uint32_t sequence = 0U; // the last input the server applied for this entity
};

constexpr float kPositionTolerance = 0.02F; // 1000 / 65535 per step
constexpr float kYawTolerance = 0.36F;      // 360 / 1023 per step

std::unique_ptr<ReplicationRegistry> MakeComponents() {
    auto registry = std::make_unique<ReplicationRegistry>();
    std::string error;
    auto position = std::make_unique<StructCodec<Position>>("Position");
    position->Float(&Position::x, -500.0F, 500.0F, 16U).Float(&Position::y, -500.0F, 500.0F, 16U).Float(&Position::z, -500.0F, 500.0F, 16U);
    Require(registry->Add(std::move(position), error), error);
    auto health = std::make_unique<StructCodec<Health>>("Health");
    health->UInt(&Health::hp, 1000U).Bool(&Health::alive);
    Require(registry->Add(std::move(health), error), error);
    auto facing = std::make_unique<StructCodec<Facing>>("Facing");
    facing->Float(&Facing::yaw, 0.0F, 360.0F, 10U);
    Require(registry->Add(std::move(facing), error), error);
    auto inputAck = std::make_unique<StructCodec<InputAck>>("InputAck");
    inputAck->UInt(&InputAck::sequence, 0xFFFFFFU);
    Require(registry->Add(std::move(inputAck), error), error);
    return registry;
}

std::unique_ptr<RpcRegistry> MakeRpcs() {
    auto registry = std::make_unique<RpcRegistry>();
    std::string error;
    const auto define = [&](RpcDefinition definition) { Require(registry->Define(std::move(definition), error), error); };
    define({ "SetTarget", RpcDirection::ClientToServer, true,
        { { RpcArgType::NetworkId }, { RpcArgType::Float, -500.0, 500.0 }, { RpcArgType::Float, -500.0, 500.0 } }, 0, 50U });
    define({ "Chat", RpcDirection::ClientToServer, true, { { RpcArgType::String, 0.0, 0.0, 32U } }, -1, 5U });
    define({ "Damage", RpcDirection::ServerToClient, true, { { RpcArgType::NetworkId }, { RpcArgType::UInt32, 0.0, 1000.0 } }, -1, 100U });
    define({ "Ping", RpcDirection::ClientToServer, false, { { RpcArgType::UInt32, 0.0, 1e9 } }, -1, 100U });
    define({ "Pong", RpcDirection::ServerToClient, false, { { RpcArgType::UInt32, 0.0, 1e9 } }, -1, 100U });
    define({ "Heal", RpcDirection::ClientToServer, true, { { RpcArgType::NetworkId }, { RpcArgType::UInt32, 1.0, 50.0 } }, 0, 20U });
    define({ "Input", RpcDirection::ClientToServer, true,
        { { RpcArgType::NetworkId }, { RpcArgType::UInt32, 1.0, 16000000.0 }, { RpcArgType::Float, -1.0, 1.0 }, { RpcArgType::Float, -1.0, 1.0 } }, 0, 200U });
    return registry;
}

SessionKey MakeKey(std::uint8_t seed) {
    SessionKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(seed + index * 5U);
    }
    return key;
}

struct Random {
    std::uint64_t state;
    std::uint64_t Next() noexcept {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }
    std::size_t Below(std::size_t limit) noexcept { return static_cast<std::size_t>(Next() % limit); }
};

// A server, a host and any number of clients on one simulated network.
struct Rig {
    struct Peer {
        std::uint64_t id = 0U;
        std::unique_ptr<IDatagramTransport> port;
        std::unique_ptr<Client> client;
        std::unique_ptr<kb::ecs::World> world;
        std::unique_ptr<ReplicationClient> replication;
        std::vector<NetEvent> events;
    };

    // With `webRtc` the server and the clients talk over WebRTC data channels on this machine instead of the simulated
    // network; the simulated link conditions then do not apply, and time is real.
    explicit Rig(LinkConditions conditions = {}, ReplicationConfig config = {}, bool webRtc = false) : network(conditions), config_(config), webRtc_(webRtc) {
        components = MakeComponents();
        rpcs = MakeRpcs();
        if (webRtc_) {
#if defined(KB_NET_WEBRTC)
            std::string error;
            listener_ = std::make_shared<SignalingListener>();
            hostPort = CreateWebRtcServerTransport(WebRtcServerOptions{ RtcConfig(), 8U }, listener_, nullptr, error);
            Require(hostPort != nullptr, error);
#endif
        } else {
            hostPort = network.CreateTransport(Endpoint::Loopback(9000));
        }
        keys.Set(1U, MakeKey(1U));
        keys.Set(2U, MakeKey(2U));
        keys.Set(3U, MakeKey(3U));
        HostConfig hostConfig;
        hostConfig.maxConnectsPerSecondPerAddress = 1024U;
        hostConfig.maxSessionsPerAddress = 64U;
        host = std::make_unique<Host>(*hostPort, hostConfig, keys);
        world = std::make_unique<kb::ecs::World>();
        server = std::make_unique<ReplicationServer>(*host, *world, *components, *rpcs, config);
    }

#if defined(KB_NET_WEBRTC)
    [[nodiscard]] static WebRtcConfig RtcConfig() {
        WebRtcConfig config;
        config.bindAddress = "127.0.0.1";
        return config;
    }
#endif

    // A client's transport to the server: a simulated port, or a WebRTC link signaled in memory.
    [[nodiscard]] std::unique_ptr<IDatagramTransport> MakePort(std::uint16_t port) {
        if (!webRtc_) {
            return network.CreateTransport(Endpoint::Loopback(port));
        }
#if defined(KB_NET_WEBRTC)
        auto [clientEnd, serverEnd] = CreateSignalingPair();
        listener_->Push(std::move(serverEnd));
        std::string error;
        auto transport = CreateWebRtcClientTransport(std::move(clientEnd), RtcConfig(), Endpoint::Loopback(9000), error);
        Require(transport != nullptr, error);
        return transport;
#else
        return nullptr;
#endif
    }

    Peer& AddPeer(std::uint64_t id, std::uint16_t port, std::uint64_t now) {
        auto peer = std::make_unique<Peer>();
        keys.Set(id, MakeKey(static_cast<std::uint8_t>(id)));
        peer->id = id;
        peer->port = MakePort(port);
        ClientConfig clientConfig;
        clientConfig.connectTimeoutMilliseconds = 60'000U;
        peer->client = std::make_unique<Client>(*peer->port, clientConfig, id, MakeKey(static_cast<std::uint8_t>(id)), Endpoint::Loopback(9000), now);
        peer->world = std::make_unique<kb::ecs::World>();
        peer->replication = std::make_unique<ReplicationClient>(*peer->client, *peer->world, *components, *rpcs, config_);
        peers.push_back(std::move(peer));
        return *peers.back();
    }

    void Step(std::uint64_t now) {
        network.Advance(now);
        host->Update(now);
        for (auto& peer : peers) {
            if (peer->client != nullptr) {
                peer->client->Update(now);
            }
        }
        NetEvent event;
        while (host->PollEvent(event)) {
            if (event.type == NetEventType::Message) {
                capturedToServer.push_back({ event.peer, event.message.channel, event.message.data });
            }
            server->HandleEvent(event, now);
        }
        for (auto& peer : peers) {
            if (peer->client == nullptr) {
                continue;
            }
            while (peer->client->PollEvent(event)) {
                if (event.type == NetEventType::Message) {
                    capturedToClient.push_back({ peer->id, event.message.channel, event.message.data });
                }
                peer->replication->HandleEvent(event);
            }
        }
        if (now >= nextTickAt) {
            nextTickAt = now + 30U;
            server->Tick(++tick, now);
        }
        for (auto& peer : peers) {
            if (peer->client != nullptr) {
                if (flushAcks) peer->replication->Flush(now);
            }
        }
    }

    // Runs 10 ms steps until `done` or `limit` ms have passed; returns the time reached.
    std::uint64_t Run(std::uint64_t from, std::uint64_t limit, const std::function<bool()>& done) {
        std::uint64_t now = from;
        for (; now < from + limit; now += 10U) {
            Step(now);
            if (done()) {
                break;
            }
            if (webRtc_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return now;
    }

    [[nodiscard]] bool AllReady() const {
        return !peers.empty() && std::ranges::all_of(peers, [](const auto& peer) { return peer->replication->Ready() && peer->client->State() == ClientState::Connected; }) &&
            server->ReadyClients() == peers.size();
    }

    // Every replicated entity that `peer` should see is there and equal to the server's, within the quantization step.
    mutable std::string why;
    [[nodiscard]] bool Converged(const Peer& peer, const std::function<bool(NetworkId)>& visible = nullptr) const {
        std::size_t expected = 0U;
        for (const auto id : server->NetworkIds()) {
            const kb::ecs::Entity serverEntity = server->EntityOf(id);
            if (!serverEntity.IsValid() || !world->IsAlive(serverEntity)) {
                continue; // not replicated, or destroyed and not yet noticed by the next Tick
            }
            if (visible != nullptr && !visible(id)) {
                if (peer.replication->Find(id).IsValid()) {
                    { why = "check 1 (peer " + std::to_string(peer.id) + ")"; return false; }
                }
                continue;
            }
            ++expected;
            const kb::ecs::Entity replica = peer.replication->Find(id);
            if (!replica.IsValid() || !peer.world->IsAlive(replica)) {
                { why = "check 2 (peer " + std::to_string(peer.id) + ")"; return false; }
            }
            const Position* sp = world->TryGet<Position>(serverEntity);
            const Position* cp = peer.world->TryGet<Position>(replica);
            if ((sp == nullptr) != (cp == nullptr)) {
                { why = "check 3 (peer " + std::to_string(peer.id) + ")"; return false; }
            }
            if (sp != nullptr && (std::fabs(sp->x - cp->x) > kPositionTolerance || std::fabs(sp->y - cp->y) > kPositionTolerance || std::fabs(sp->z - cp->z) > kPositionTolerance)) {
                { why = "position mismatch peer=" + std::to_string(peer.id) + " id=" + std::to_string(id) + " server=" + std::to_string(sp->x) + " replica=" + std::to_string(cp->x); return false; }
            }
            const Health* sh = world->TryGet<Health>(serverEntity);
            const Health* ch = peer.world->TryGet<Health>(replica);
            if ((sh == nullptr) != (ch == nullptr) || (sh != nullptr && (sh->hp != ch->hp || sh->alive != ch->alive))) {
                { why = "check 5 (peer " + std::to_string(peer.id) + ")"; return false; }
            }
            const Facing* sf = world->TryGet<Facing>(serverEntity);
            const Facing* cf = peer.world->TryGet<Facing>(replica);
            if ((sf == nullptr) != (cf == nullptr) || (sf != nullptr && std::fabs(sf->yaw - cf->yaw) > kYawTolerance)) {
                { why = "check 6 (peer " + std::to_string(peer.id) + ")"; return false; }
            }
            if (peer.world->TryGet<Secret>(replica) != nullptr) {
                { why = "check 7 (peer " + std::to_string(peer.id) + ")"; return false; }
            }
            if (peer.replication->OwnerOf(id) != server->OwnerOf(id)) {
                { why = "check 8 (peer " + std::to_string(peer.id) + ")"; return false; }
            }
        }
        if (peer.replication->ReplicaCount() != expected) {
            why = "count: peer " + std::to_string(peer.id) + " has " + std::to_string(peer.replication->ReplicaCount()) + " expected " + std::to_string(expected);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool AllConverged() const {
        return std::ranges::all_of(peers, [this](const auto& peer) { return peer->client == nullptr || Converged(*peer); });
    }

    kb::ecs::Entity Spawn(float x, float y, std::uint64_t owner, NetworkId* id = nullptr) {
        const kb::ecs::Entity entity = world->CreateEntity();
        Move(entity, Position{ x, y, 0.0F });
        world->Set(entity, Health{});
        world->Set(entity, Facing{ 90.0F });
        world->Set(entity, Secret{ 7 });
        Require(server->Replicate(entity, owner) == ReplicateResult::Ok, "entity was not replicated");
        const NetworkId networkId = server->NetworkIdOf(entity);
        Require(networkId != 0U, "an entity could not be replicated");
        if (id != nullptr) {
            *id = networkId;
        }
        return entity;
    }

    void Move(kb::ecs::Entity entity, Position position) {
        world->Set(entity, position);
        if (config_.visibilityRadius > 0.0F) {
            kb::scene::TransformComponent transform;
            transform.localPosition = transform.worldPosition = { position.x, position.y, position.z };
            transform.worldDirty = false;
            world->Set(entity, transform);
        }
    }

    struct Captured {
        std::uint64_t peer;
        std::uint8_t channel;
        std::vector<std::uint8_t> data;
    };

    LoopbackNetwork network;
    bool flushAcks = true;
    ReplicationConfig config_;
    bool webRtc_ = false;
#if defined(KB_NET_WEBRTC)
    std::shared_ptr<SignalingListener> listener_;
#endif
    std::unique_ptr<ReplicationRegistry> components;
    std::unique_ptr<RpcRegistry> rpcs;
    std::unique_ptr<IDatagramTransport> hostPort;
    StaticKeyTable keys;
    std::unique_ptr<Host> host;
    std::unique_ptr<kb::ecs::World> world;
    std::unique_ptr<ReplicationServer> server;
    std::vector<std::unique_ptr<Peer>> peers;
    std::vector<Captured> capturedToServer;
    std::vector<Captured> capturedToClient;
    std::uint32_t tick = 0U;
    std::uint64_t nextTickAt = 0U;
};

LinkConditions BadLink(std::uint64_t seed = 11U) {
    LinkConditions conditions;
    conditions.simulation.latencyMilliseconds = 40U;
    conditions.simulation.jitterMilliseconds = 20U;
    conditions.simulation.lossPermille = 250U;
    conditions.simulation.reorderPermille = 200U;
    conditions.simulation.seed = seed;
    conditions.duplicatePermille = 100U;
    return conditions;
}

// ------------------------------------------------------------------ tests

void TestCodecs() {
    kb::ecs::World world;
    const kb::ecs::Entity entity = world.CreateEntity();
    world.Set(entity, Position{ 12.345F, -499.9F, 1000.0F }); // z is outside the range and is clamped
    StructCodec<Position> codec("Position");
    codec.Float(&Position::x, -500.0F, 500.0F, 16U).Float(&Position::y, -500.0F, 500.0F, 16U).Float(&Position::z, -500.0F, 500.0F, 16U);
    Require(codec.Valid() && codec.EncodedSize() == 6U, "the position codec has the wrong size");
    std::vector<std::uint8_t> bytes(codec.EncodedSize());
    Require(codec.Capture(world, entity, bytes), "capture failed");
    Require(codec.Validate(bytes), "captured bytes did not validate");
    kb::ecs::World other;
    const kb::ecs::Entity copy = other.CreateEntity();
    codec.Apply(other, copy, bytes);
    const Position* applied = other.TryGet<Position>(copy);
    Require(applied != nullptr && std::fabs(applied->x - 12.345F) < kPositionTolerance && std::fabs(applied->y + 499.9F) < kPositionTolerance &&
            std::fabs(applied->z - 500.0F) < kPositionTolerance,
        "values did not round trip within the quantization step");
    // A value inside the same step encodes to the same bytes: a tiny change costs nothing.
    world.Set(entity, Position{ 12.3451F, -499.9F, 1000.0F });
    std::vector<std::uint8_t> again(codec.EncodedSize());
    Require(codec.Capture(world, entity, again) && again == bytes, "a change below the quantization step changed the encoding");
    // NaN does not escape into the encoding.
    world.Set(entity, Position{ std::nanf(""), 0.0F, 0.0F });
    Require(codec.Capture(world, entity, again) && codec.Validate(again), "a NaN produced invalid bytes");
    // The entity without the component.
    const kb::ecs::Entity bare = world.CreateEntity();
    Require(!codec.Capture(world, bare, again), "capture succeeded for an entity without the component");

    StructCodec<Health> health("Health");
    health.UInt(&Health::hp, 1000U).Bool(&Health::alive);
    Require(health.EncodedSize() == 3U, "the health codec has the wrong size");
    Require(health.Validate(std::vector<std::uint8_t>{ 0xE8U, 0x03U, 1U }), "the maximum value was refused");
    Require(!health.Validate(std::vector<std::uint8_t>{ 0xE9U, 0x03U, 1U }), "a value above the maximum was accepted");
    Require(!health.Validate(std::vector<std::uint8_t>{ 0U, 0U, 2U }), "a bool of 2 was accepted");
    Require(!health.Validate(std::vector<std::uint8_t>{ 0U, 0U }), "a short buffer was accepted");
    StructCodec<Facing> facing("Facing");
    facing.Float(&Facing::yaw, 0.0F, 360.0F, 10U);
    Require(facing.Validate(std::vector<std::uint8_t>{ 0xFFU, 0x03U }) && !facing.Validate(std::vector<std::uint8_t>{ 0x00U, 0x04U }),
        "the quantized range is not enforced");
    StructCodec<Facing> broken("Broken");
    broken.Float(&Facing::yaw, 5.0F, 5.0F, 10U);
    Require(!broken.Valid(), "an empty range was accepted");
    StructCodec<Facing> tooManyBits("Bits");
    tooManyBits.Float(&Facing::yaw, 0.0F, 1.0F, 25U);
    Require(!tooManyBits.Valid(), "25 bits were accepted");

    // The registry refuses invalid and repeated codecs and changes its fingerprint with the layout.
    ReplicationRegistry registry;
    std::string error;
    Require(!registry.Add(std::make_unique<StructCodec<Facing>>("Empty"), error), "a codec without fields was accepted");
    auto first = std::make_unique<StructCodec<Facing>>("Facing");
    first->Float(&Facing::yaw, 0.0F, 360.0F, 10U);
    Require(registry.Add(std::move(first), error), error);
    auto repeated = std::make_unique<StructCodec<Facing>>("Facing");
    repeated->Float(&Facing::yaw, 0.0F, 360.0F, 10U);
    Require(!registry.Add(std::move(repeated), error), "a repeated name was accepted");
    ReplicationRegistry coarser;
    auto other10 = std::make_unique<StructCodec<Facing>>("Facing");
    other10->Float(&Facing::yaw, 0.0F, 360.0F, 9U);
    Require(coarser.Add(std::move(other10), error), error);
    Require(registry.Fingerprint() != coarser.Fingerprint() && MakeComponents()->Fingerprint() == MakeComponents()->Fingerprint(),
        "the fingerprint does not follow the layout");
}

void TestRpcRegistryAndWire() {
    auto rpcs = MakeRpcs();
    std::string error;
    RpcRegistry scratch;
    Require(!scratch.Define({ "", RpcDirection::ClientToServer, true, {}, -1, 1U }, error), "an unnamed RPC was accepted");
    Require(scratch.Define({ "A", RpcDirection::ClientToServer, true, {}, -1, 1U }, error), error);
    Require(!scratch.Define({ "A", RpcDirection::ClientToServer, true, {}, -1, 1U }, error), "a repeated name was accepted");
    Require(!scratch.Define({ "B", RpcDirection::ClientToServer, true, { { RpcArgType::UInt32 } }, 0, 1U }, error), "an owner argument that is not a NetworkId was accepted");
    Require(!scratch.Define({ "C", RpcDirection::ServerToClient, true, { { RpcArgType::NetworkId } }, 0, 1U }, error), "an owner argument on a server call was accepted");
    Require(!scratch.Define({ "D", RpcDirection::ClientToServer, true, { { RpcArgType::Float, 5.0, 1.0 } }, -1, 1U }, error), "an inverted range was accepted");
    Require(!scratch.Define({ "E", RpcDirection::ClientToServer, true, std::vector<RpcArgSpec>(kMaxRpcArgs + 1U), -1, 1U }, error), "too many arguments were accepted");
    Require(!scratch.Define({ "F", RpcDirection::ClientToServer, true, {}, -1, 0U }, error), "an RPC that allows no calls was accepted");
    Require(rpcs->Fingerprint() == MakeRpcs()->Fingerprint() && rpcs->Fingerprint() != scratch.Fingerprint(), "the RPC fingerprint is wrong");

    std::uint16_t id = 0U;
    const RpcDefinition* setTarget = rpcs->FindByName("SetTarget", id);
    Require(setTarget != nullptr && id == 1U && rpcs->Find(id) == setTarget && rpcs->Find(0U) == nullptr && rpcs->Find(99U) == nullptr, "lookup is wrong");

    const std::vector<RpcValue> good{ MakeNetworkId(7U, 123U), 12.5F, -3.0F };
    std::vector<std::uint8_t> wire;
    Require(EncodeRpc(*rpcs, id, good, wire) == RpcStatus::Ok, "encoding failed");
    std::uint16_t decodedId = 0U;
    std::vector<RpcValue> decoded;
    Require(DecodeRpc(*rpcs, wire, RpcDirection::ClientToServer, decodedId, decoded) == RpcStatus::Ok && decodedId == id && decoded == good, "the call did not round trip");
    Require(DecodeRpc(*rpcs, wire, RpcDirection::ServerToClient, decodedId, decoded) == RpcStatus::WrongDirection, "the wrong direction was accepted");

    // Every kind of argument round trips.
    std::uint16_t chatId = 0U;
    static_cast<void>(rpcs->FindByName("Chat", chatId));
    std::vector<std::uint8_t> chat;
    Require(EncodeRpc(*rpcs, chatId, std::vector<RpcValue>{ std::string("hello") }, chat) == RpcStatus::Ok, "chat did not encode");
    Require(DecodeRpc(*rpcs, chat, RpcDirection::ClientToServer, decodedId, decoded) == RpcStatus::Ok && std::get<std::string>(decoded[0]) == "hello", "chat did not round trip");

    // Arguments out of range, of the wrong type or count, and strings that are too long.
    std::vector<std::uint8_t> sink;
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ NetworkId{ 0U }, 1.0F, 1.0F }, sink) == RpcStatus::BadArguments, "network id 0 was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ NetworkId{ 1U }, 1000.0F, 1.0F }, sink) == RpcStatus::BadArguments, "a float out of range was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ NetworkId{ 1U }, std::nanf(""), 1.0F }, sink) == RpcStatus::BadArguments, "NaN was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ NetworkId{ 1U }, 1.0F }, sink) == RpcStatus::BadArguments, "a missing argument was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ std::int32_t{ 1 }, 1.0F, 1.0F }, sink) == RpcStatus::BadArguments, "a wrong type was accepted");
    Require(EncodeRpc(*rpcs, chatId, std::vector<RpcValue>{ std::string(33U, 'x') }, sink) == RpcStatus::BadArguments, "a long string was accepted");
    Require(EncodeRpc(*rpcs, 77U, std::vector<RpcValue>{}, sink) == RpcStatus::UnknownRpc, "an unknown id was accepted");
    Require(sink.empty(), "a rejected call left bytes in the buffer");

    // Bad bytes.
    for (std::size_t length = 0U; length < wire.size(); ++length) {
        Require(DecodeRpc(*rpcs, std::span<const std::uint8_t>(wire.data(), length), RpcDirection::ClientToServer, decodedId, decoded) != RpcStatus::Ok,
            "a truncated call was accepted");
    }
    std::vector<std::uint8_t> trailing = wire;
    trailing.push_back(0U);
    Require(DecodeRpc(*rpcs, trailing, RpcDirection::ClientToServer, decodedId, decoded) == RpcStatus::Malformed, "trailing bytes were accepted");
    std::vector<std::uint8_t> nan = wire;
    nan[10U] = 0x00U; nan[11U] = 0x00U; nan[12U] = 0xC0U; nan[13U] = 0x7FU; // the first float becomes a NaN
    Require(DecodeRpc(*rpcs, nan, RpcDirection::ClientToServer, decodedId, decoded) == RpcStatus::BadArguments, "a NaN on the wire was accepted");
    std::vector<std::uint8_t> longString = chat;
    longString[2U] = 0xFFU; longString[3U] = 0xFFU; // declares 65535 bytes
    Require(DecodeRpc(*rpcs, longString, RpcDirection::ClientToServer, decodedId, decoded) != RpcStatus::Ok, "an oversized string length was accepted");
    Require(DecodeRpc(*rpcs, std::vector<std::uint8_t>(kMaxRpcPayloadBytes + 1U, 1U), RpcDirection::ClientToServer, decodedId, decoded) == RpcStatus::TooLarge,
        "an oversized payload was accepted");

    // The rate limiter allows a burst, then the sustained rate.
    RpcRateLimiter limiter;
    std::size_t allowed = 0U;
    for (int call = 0; call < 100; ++call) {
        allowed += limiter.Allow(1U, 1U, 10U, 0U) ? 1U : 0U;
    }
    Require(allowed == 10U, "the burst was " + std::to_string(allowed));
    Require(limiter.Allow(1U, 1U, 10U, 200U) && limiter.Allow(1U, 1U, 10U, 200U) && !limiter.Allow(1U, 1U, 10U, 200U), "the bucket did not refill at the rate");
    Require(limiter.Allow(2U, 1U, 10U, 200U), "callers share a bucket");
    limiter.Forget(1U);
    Require(limiter.Allow(1U, 1U, 10U, 200U), "a forgotten caller keeps its bucket");
}

void RunPerfectNetworkScenario(bool webRtc) {
    Rig rig({}, {}, webRtc);
    rig.AddPeer(1U, 9101U, 0U);
    rig.AddPeer(2U, 9102U, 0U);
    std::vector<kb::ecs::Entity> entities;
    std::vector<NetworkId> ids;
    for (int index = 0; index < 5; ++index) {
        NetworkId id = 0U;
        entities.push_back(rig.Spawn(static_cast<float>(index) * 10.0F, 0.0F, index % 2 == 0 ? 1U : 2U, &id));
        ids.push_back(id);
    }
    std::uint64_t now = rig.Run(0U, 10'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllReady() && rig.AllConverged(), "the clients did not receive the initial state");
    for (const auto& peer : rig.peers) {
        Require(peer->replication->ReplicaCount() == 5U && peer->replication->Stats().spawnsApplied == 5U, "a client did not get every spawn");
    }

    // Movement and component changes replicate; a component that is not registered never does.
    for (int step = 1; step <= 60; ++step) {
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            rig.world->Set(entities[index], Position{ static_cast<float>(index) * 10.0F + static_cast<float>(step) * 1.5F, static_cast<float>(step) * 0.5F, 0.0F });
        }
        now = rig.Run(now, 30U, [] { return false; });
    }
    rig.world->Set(entities[1], Health{ 640U, false });
    rig.world->Set(entities[2], Facing{ 271.0F });
    now = rig.Run(now, 5000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged(), "the clients did not converge after movement");

    // Removing a replicated component removes it on the clients.
    rig.world->Remove<Facing>(entities[3]);
    now = rig.Run(now, 5000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged() && !rig.peers[0]->world->Has<Facing>(rig.peers[0]->replication->Find(ids[3])), "a removed component stayed on the client");

    // Ownership change.
    Require(rig.server->SetOwner(ids[0], 2U), "ownership could not change");
    now = rig.Run(now, 5000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged() && rig.peers[0]->replication->OwnerOf(ids[0]) == 2U, "the new owner did not reach the clients");

    // Despawn: by Unreplicate and by destroying the entity. Clients keep no stale entity.
    const kb::ecs::Entity gone = rig.peers[0]->replication->Find(ids[4]);
    Require(rig.server->Unreplicate(ids[4]), "unreplicate failed");
    rig.world->DestroyEntity(entities[1]);
    now = rig.Run(now, 5000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged() && rig.peers[0]->replication->ReplicaCount() == 3U, "despawns did not reach the clients");
    Require(!rig.peers[0]->world->IsAlive(gone), "a despawned replica is still alive on the client");
    Require(rig.server->ReplicatedCount() == 3U, "the server holds despawned entities");

    // Late join: a third client gets everything.
    rig.AddPeer(3U, 9103U, now);
    now = rig.Run(now, 10'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllReady() && rig.AllConverged() && rig.peers[2]->replication->ReplicaCount() == 3U, "a late joiner did not converge");

    // Reconnect: a client that goes away and comes back from another address gets the whole state again.
    rig.peers[1]->client.reset();
    rig.peers[1]->replication.reset();
    rig.peers[1]->port.reset();
    rig.peers[1]->world = std::make_unique<kb::ecs::World>();
    {
        auto port = rig.MakePort(9104U);
        ClientConfig clientConfig;
        clientConfig.connectTimeoutMilliseconds = 60'000U;
        rig.peers[1]->port = std::move(port);
        rig.peers[1]->client = std::make_unique<Client>(*rig.peers[1]->port, clientConfig, 2U, MakeKey(2U), Endpoint::Loopback(9000), now);
        rig.peers[1]->replication = std::make_unique<ReplicationClient>(*rig.peers[1]->client, *rig.peers[1]->world, *rig.components, *rig.rpcs, ReplicationConfig{});
    }
    rig.world->Set(entities[0], Position{ 100.0F, 100.0F, 0.0F });
    now = rig.Run(now, 20'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllReady() && rig.AllConverged() && rig.peers[1]->replication->ReplicaCount() == 3U, "a reconnected client did not converge");
    Require(rig.server->Stats().despawnsSent >= 2U && rig.server->Stats().entriesSent > 0U, "the server statistics are empty");
}

void TestReplicationOnPerfectNetwork() {
    RunPerfectNetworkScenario(false);
}

#if defined(KB_NET_WEBRTC)
// The same scenario with the server and its clients on WebRTC data channels.
void TestReplicationOverWebRtc() {
    RunPerfectNetworkScenario(true);
}
#endif

void RunBadNetworkScenario(std::uint64_t seed, int steps, int entityCount, bool report) {
    Rig rig(BadLink(seed));
    rig.AddPeer(1U, 9101U, 0U);
    rig.AddPeer(2U, 9102U, 0U);
    std::vector<kb::ecs::Entity> entities;
    for (int index = 0; index < entityCount; ++index) {
        entities.push_back(rig.Spawn(static_cast<float>(index), static_cast<float>(-index), index % 2 == 0 ? 1U : 2U));
    }
    Random random{ 0x1234ABCDULL + seed };
    std::uint64_t now = 0U;
    for (int step = 0; step < steps; ++step) {
        for (const kb::ecs::Entity entity : entities) {
            if (random.Below(3U) == 0U) {
                rig.world->Set(entity, Position{ static_cast<float>(random.Below(1000U)) - 500.0F, static_cast<float>(random.Below(1000U)) - 500.0F, 0.0F });
            }
            if (random.Below(10U) == 0U) {
                rig.world->Set(entity, Health{ static_cast<std::uint32_t>(random.Below(1001U)), random.Below(2U) == 0U });
            }
        }
        now = rig.Run(now, 30U, [] { return false; });
    }
    // Stop changing things: the clients must reach the final state of the server.
    const std::uint64_t settle = rig.Run(now, 30'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllConverged(), "the clients did not converge on a bad network (seed " + std::to_string(seed) + "): " + rig.why);
    // Churn while the network misbehaves: spawn, move, destroy.
    for (int round = 0; round < 40; ++round) {
        if (round % 3 == 0 && !entities.empty()) {
            rig.world->DestroyEntity(entities.back());
            entities.pop_back();
        } else {
            entities.push_back(rig.Spawn(static_cast<float>(round), 5.0F, 1U));
        }
        for (const kb::ecs::Entity entity : entities) {
            rig.world->Set(entity, Position{ static_cast<float>(random.Below(1000U)) - 500.0F, 0.0F, 0.0F });
        }
        now = rig.Run(settle + static_cast<std::uint64_t>(round) * 100U, 100U, [] { return false; });
    }
    now = rig.Run(now, 60'000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged(), "the clients did not converge after spawn and despawn churn on a bad network (seed " + std::to_string(seed) + "): " + rig.why);
    const LoopbackStats& link = rig.network.Stats();
    Require(link.dropped > 100U && link.duplicated > 10U && link.reordered > 10U, "the network did not misbehave");
    const ReplicationClientStats& stats = rig.peers[0]->replication->Stats();
    Require(stats.entriesApplied > 0U && stats.rejectedMalformed == 0U, "a genuine message was rejected as malformed");
    if (!report) {
        return;
    }
    std::printf("  replication report: link sent %llu dropped %llu; server sent %llu state messages, %llu entries, %llu spawns, %llu despawns; client 1 applied %llu entries, "
                "%llu stale, %llu for unknown entities\n",
        static_cast<unsigned long long>(link.sent), static_cast<unsigned long long>(link.dropped), static_cast<unsigned long long>(rig.server->Stats().stateMessagesSent),
        static_cast<unsigned long long>(rig.server->Stats().entriesSent), static_cast<unsigned long long>(rig.server->Stats().spawnsSent),
        static_cast<unsigned long long>(rig.server->Stats().despawnsSent), static_cast<unsigned long long>(stats.entriesApplied),
        static_cast<unsigned long long>(stats.entriesStale), static_cast<unsigned long long>(stats.entriesUnknownEntity));
}

void TestReplicationOnBadNetwork() {
    RunBadNetworkScenario(11U, 300, 20, true);
}

void TestReplicationManySeeds() {
    for (std::uint64_t seed = 1U; seed <= 24U; ++seed) {
        RunBadNetworkScenario(seed, 80, 8, false);
    }
}

struct Calls {
    std::vector<std::pair<std::uint64_t, std::vector<RpcValue>>> server;
    std::vector<std::vector<RpcValue>> client;
};

void TestRpcAndPermissions() {
    Rig rig(BadLink());
    Rig::Peer& alice = rig.AddPeer(1U, 9101U, 0U);
    Rig::Peer& bob = rig.AddPeer(2U, 9102U, 0U);
    NetworkId aliceId = 0U;
    NetworkId bobId = 0U;
    const kb::ecs::Entity aliceEntity = rig.Spawn(0.0F, 0.0F, 1U, &aliceId);
    const kb::ecs::Entity bobEntity = rig.Spawn(10.0F, 0.0F, 2U, &bobId);
    static_cast<void>(bobEntity);
    Calls calls;
    std::vector<std::uint32_t> pings;
    rig.server->SetRpcHandler("SetTarget", [&](std::uint64_t caller, std::span<const RpcValue> args) { calls.server.emplace_back(caller, std::vector<RpcValue>(args.begin(), args.end())); });
    rig.server->SetRpcHandler("Chat", [&](std::uint64_t caller, std::span<const RpcValue> args) { calls.server.emplace_back(caller, std::vector<RpcValue>(args.begin(), args.end())); });
    rig.server->SetRpcHandler("Ping", [&](std::uint64_t caller, std::span<const RpcValue> args) {
        pings.push_back(std::get<std::uint32_t>(args[0]));
        const std::vector<RpcValue> reply{ std::get<std::uint32_t>(args[0]) };
        static_cast<void>(rig.server->CallRpc(caller, "Pong", reply));
    });
    // Heal changes server state: only the owner of the entity may do it.
    rig.server->SetRpcHandler("Heal", [&](std::uint64_t, std::span<const RpcValue> args) {
        const kb::ecs::Entity target = rig.server->EntityOf(std::get<NetworkId>(args[0]));
        Health health = *rig.world->TryGet<Health>(target);
        health.hp = std::min<std::uint32_t>(1000U, health.hp + std::get<std::uint32_t>(args[1]));
        rig.world->Set(target, health);
    });
    std::vector<std::uint32_t> pongs;
    std::vector<std::vector<RpcValue>> damages;
    alice.replication->SetRpcHandler("Pong", [&](std::span<const RpcValue> args) { pongs.push_back(std::get<std::uint32_t>(args[0])); });
    alice.replication->SetRpcHandler("Damage", [&](std::span<const RpcValue> args) { damages.emplace_back(args.begin(), args.end()); });
    std::vector<std::vector<RpcValue>> bobDamages;
    bob.replication->SetRpcHandler("Damage", [&](std::span<const RpcValue> args) { bobDamages.emplace_back(args.begin(), args.end()); });

    std::uint64_t now = rig.Run(0U, 30'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllReady(), "the clients did not become ready");

    // Calls before the connection is ready are refused locally; the unknown name and the wrong direction as well.
    const std::vector<RpcValue> target{ aliceId, 3.0F, 4.0F };
    Require(alice.replication->CallRpc("Nope", target) == RpcStatus::UnknownRpc, "an unknown RPC was called");
    Require(alice.replication->CallRpc("Damage", target) == RpcStatus::WrongDirection, "a server RPC was called by a client");
    Require(rig.server->CallRpc(1U, "SetTarget", target) == RpcStatus::WrongDirection, "a client RPC was called by the server");
    Require(alice.replication->CallRpc("SetTarget", std::vector<RpcValue>{ aliceId, 600.0F, 400.0F }) == RpcStatus::BadArguments, "arguments out of range were sent");

    // Client to server, reliable, on the entity the caller owns: arrives once with the caller's identity.
    Require(alice.replication->CallRpc("SetTarget", target) == RpcStatus::Ok, "the call could not be sent");
    Require(alice.replication->CallRpc("Chat", std::vector<RpcValue>{ std::string("hello") }) == RpcStatus::Ok, "chat could not be sent");
    now = rig.Run(now, 20'000U, [&] { return calls.server.size() >= 2U; });
    Require(calls.server.size() == 2U && calls.server[0].first == 1U && calls.server[0].second == target && calls.server[1].first == 1U &&
            std::get<std::string>(calls.server[1].second[0]) == "hello",
        "the calls did not arrive once, in order, with the caller's identity");

    // Without permission: bob names alice's entity. Nothing is called, the refusal is counted.
    const std::size_t before = calls.server.size();
    Require(bob.replication->CallRpc("SetTarget", std::vector<RpcValue>{ aliceId, 1.0F, 1.0F }) == RpcStatus::Ok, "bob's call could not be sent");
    Require(bob.replication->CallRpc("SetTarget", std::vector<RpcValue>{ NetworkId{ 999U }, 1.0F, 1.0F }) == RpcStatus::Ok, "bob's call on a missing entity could not be sent");
    now = rig.Run(now, 5000U, [&] { return rig.server->Stats().rpcRejectedPermission >= 2U; });
    Require(rig.server->Stats().rpcRejectedPermission >= 2U && calls.server.size() == before, "a call without permission reached the handler");

    // The same for a call that changes server state: bob cannot heal alice's entity, alice can heal her own.
    Require(bob.replication->CallRpc("Heal", std::vector<RpcValue>{ aliceId, std::uint32_t{ 40U } }) == RpcStatus::Ok, "heal could not be sent");
    now = rig.Run(now, 3000U, [&] { return rig.server->Stats().rpcRejectedPermission >= 3U; });
    Require(rig.world->TryGet<Health>(aliceEntity)->hp == 100U, "a client without permission changed the server state");
    Require(alice.replication->CallRpc("Heal", std::vector<RpcValue>{ aliceId, std::uint32_t{ 40U } }) == RpcStatus::Ok, "heal could not be sent");
    now = rig.Run(now, 20'000U, [&] { return rig.world->TryGet<Health>(aliceEntity)->hp == 140U && rig.AllConverged(); });
    Require(rig.world->TryGet<Health>(aliceEntity)->hp == 140U && rig.AllConverged(), "the owner's call did not change the server state and replicate");

    // A client cannot push state or structure at the server: those message types are not accepted from clients.
    const std::uint64_t malformed = rig.server->Stats().rejectedMalformed;
    std::vector<std::uint8_t> forgedState{ 0x10U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 0U, 0U };
    std::vector<std::uint8_t> forgedSpawn{ 2U, 50U, 0U, 0U, 0U };
    std::vector<std::uint8_t> forgedDespawn{ 3U, static_cast<std::uint8_t>(aliceId), 0U, 0U, 0U };
    std::vector<std::uint8_t> forgedOwner{ 4U, static_cast<std::uint8_t>(aliceId), 0U, 0U, 0U, 2U, 0U, 0U, 0U, 0U, 0U, 0U, 0U };
    Require(bob.client->Send(1U, forgedState) == SendResult::Queued && bob.client->Send(0U, forgedSpawn) == SendResult::Queued &&
            bob.client->Send(0U, forgedDespawn) == SendResult::Queued && bob.client->Send(0U, forgedOwner) == SendResult::Queued,
        "the forged messages could not be sent");
    now = rig.Run(now, 20'000U, [&] { return rig.server->Stats().rejectedMalformed >= malformed + 4U; });
    Require(rig.server->Stats().rejectedMalformed >= malformed + 4U, "forged state messages were not refused");
    Require(rig.server->OwnerOf(aliceId) == 1U && rig.server->ReplicatedCount() == 2U && rig.world->IsAlive(aliceEntity), "a forged message changed the server");

    // A server call that a client sends (wrong direction on the wire): crafted by hand.
    std::vector<std::uint8_t> wrongWay{ 5U };
    std::uint16_t damageId = 0U;
    static_cast<void>(rig.rpcs->FindByName("Damage", damageId));
    Require(EncodeRpc(*rig.rpcs, damageId, std::vector<RpcValue>{ aliceId, std::uint32_t{ 5U } }, wrongWay) == RpcStatus::Ok, "encoding failed");
    const std::uint64_t directionBefore = rig.server->Stats().rpcRejectedDirection;
    Require(bob.client->Send(0U, wrongWay) == SendResult::Queued, "the crafted call could not be sent");
    now = rig.Run(now, 5000U, [&] { return rig.server->Stats().rpcRejectedDirection > directionBefore; });
    Require(rig.server->Stats().rpcRejectedDirection > directionBefore, "a call in the wrong direction was not refused");

    // Server to client, reliable, to one and to all; unreliable both ways.
    Require(rig.server->CallRpc(1U, "Damage", std::vector<RpcValue>{ aliceId, std::uint32_t{ 25U } }) == RpcStatus::Ok, "the server call failed");
    RpcStatus status = RpcStatus::Busy;
    rig.server->CallRpcAll("Damage", std::vector<RpcValue>{ bobId, std::uint32_t{ 10U } }, status);
    Require(status == RpcStatus::Ok, "the broadcast failed");
    for (std::uint32_t value = 1U; value <= 20U; ++value) {
        Require(alice.replication->CallRpc("Ping", std::vector<RpcValue>{ value }) == RpcStatus::Ok, "ping failed");
        now = rig.Run(now, 60U, [] { return false; });
    }
    now = rig.Run(now, 20'000U, [&] { return damages.size() >= 2U && bobDamages.size() >= 1U; });
    Require(damages.size() == 2U && bobDamages.size() == 1U, "server calls did not arrive exactly once");
    Require(!pings.empty() && !pongs.empty() && pings.size() <= 20U && pongs.size() <= pings.size(), "unreliable calls behaved wrongly");
    Require(std::is_sorted(pings.begin(), pings.end()) && std::adjacent_find(pings.begin(), pings.end()) == pings.end(), "an unreliable call arrived twice or out of order");

    // Rate limit: Chat allows 5 per second.
    const std::size_t chatBefore = calls.server.size();
    for (int index = 0; index < 40; ++index) {
        static_cast<void>(alice.replication->CallRpc("Chat", std::vector<RpcValue>{ std::string("spam") }));
    }
    now = rig.Run(now, 5000U, [] { return false; });
    Require(rig.server->Stats().rpcRejectedRate > 0U && calls.server.size() - chatBefore <= 12U, "the rate limit did not hold: " + std::to_string(calls.server.size() - chatBefore));
    Require(alice.client->State() == ClientState::Connected && bob.client->State() == ClientState::Connected, "a client was dropped");
}

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};
struct MoveInput {
    float x = 0.0F;
    float y = 0.0F;
};

Vec2 StepMove(const Vec2& state, const MoveInput& input, float dt, bool wall) {
    Vec2 next{ state.x + input.x * 10.0F * dt, state.y + input.y * 10.0F * dt };
    if (wall) {
        next.x = std::min(next.x, 50.0F);
    }
    return next;
}

// The client predicts its own movement, the server applies the same inputs (and, if `wall`, a wall the
// client does not know about), and the client reconciles with the replicated state.
void RunPredictionScenario(bool wall) {
    constexpr float kDt = 0.03F;
    Rig rig(BadLink(5U));
    Rig::Peer& peer = rig.AddPeer(1U, 9101U, 0U);
    const kb::ecs::Entity entity = rig.world->CreateEntity();
    rig.world->Set(entity, Position{});
    rig.world->Set(entity, InputAck{});
    Require(rig.server->Replicate(entity, 1U) == ReplicateResult::Ok, "entity was not replicated");
    const NetworkId id = rig.server->NetworkIdOf(entity);
    rig.server->SetRpcHandler("Input", [&](std::uint64_t, std::span<const RpcValue> args) {
        const kb::ecs::Entity target = rig.server->EntityOf(std::get<NetworkId>(args[0]));
        const Position current = *rig.world->TryGet<Position>(target);
        const Vec2 next = StepMove(Vec2{ current.x, current.y }, MoveInput{ std::get<float>(args[2]), std::get<float>(args[3]) }, kDt, wall);
        rig.world->Set(target, Position{ next.x, next.y, 0.0F });
        rig.world->Set(target, InputAck{ std::get<std::uint32_t>(args[1]) });
    });
    ClientPrediction<Vec2, MoveInput> prediction(
        [](const Vec2& state, const MoveInput& input, float dt) { return StepMove(state, input, dt, false); },
        [](const Vec2& left, const Vec2& right) { return std::hypot(static_cast<double>(left.x - right.x), static_cast<double>(left.y - right.y)); },
        Vec2{}, kDt, 0.05);

    std::uint64_t now = rig.Run(0U, 30'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllReady(), "the client did not become ready");
    constexpr int kInputs = 200;
    int sent = 0;
    std::uint32_t lastAck = 0U;
    double maxLead = 0.0;
    std::size_t maxPending = 0U;
    for (const std::uint64_t end = now + 120'000U; now < end; now += 10U) {
        if (sent < kInputs && now % 30U == 0U) {
            const MoveInput input{ 1.0F, sent % 40 < 20 ? 0.5F : -0.5F };
            const std::uint32_t sequence = prediction.Predict(input);
            Require(sequence != 0U, "the prediction refused an input");
            Require(peer.replication->CallRpc("Input", std::vector<RpcValue>{ id, sequence, input.x, input.y }) == RpcStatus::Ok, "the input could not be sent");
            ++sent;
        }
        rig.Step(now);
        maxPending = std::max(maxPending, prediction.Pending());
        const kb::ecs::Entity replica = peer.replication->Find(id);
        if (replica.IsValid()) {
            const Position* position = peer.world->TryGet<Position>(replica);
            const InputAck* ack = peer.world->TryGet<InputAck>(replica);
            if (position != nullptr && ack != nullptr) {
                maxLead = std::max(maxLead, static_cast<double>(prediction.Current().x - position->x));
                if (ack->sequence != lastAck) {
                    lastAck = ack->sequence;
                    static_cast<void>(prediction.Reconcile(Vec2{ position->x, position->y }, ack->sequence));
                }
            }
        }
        if (sent == kInputs && prediction.Pending() == 0U && rig.AllConverged() && lastAck == static_cast<std::uint32_t>(kInputs)) {
            break;
        }
    }
    const Position server = *rig.world->TryGet<Position>(entity);
    const double finalError = std::hypot(static_cast<double>(prediction.Current().x - server.x), static_cast<double>(prediction.Current().y - server.y));
    Require(lastAck == static_cast<std::uint32_t>(kInputs) && prediction.Pending() == 0U, "the server did not process every input");
    Require(finalError < 0.05, "the prediction did not converge on the server's state: " + std::to_string(finalError));
    Require(maxLead > 1.0, "the prediction never ran ahead of the replicated state");
    Require(maxPending <= 128U, "the input history is not bounded");
    const auto& stats = prediction.GetStats();
    if (wall) {
        Require(server.x == 50.0F, "the server's wall did not hold");
        Require(stats.corrections > 0U && stats.largestError > 1.0, "the wall the client did not know about caused no correction");
    } else {
        Require(stats.corrections == 0U && stats.largestError < 0.05, "a prediction that matches the server was corrected: " + std::to_string(stats.largestError));
    }
    std::printf("  prediction (%s): %llu reconciliations, %llu corrections, largest correction %.3f, ran ahead by up to %.1f, history up to %zu inputs\n",
        wall ? "with a wall only the server knows" : "same rules", static_cast<unsigned long long>(stats.reconciliations),
        static_cast<unsigned long long>(stats.corrections), stats.largestError, maxLead, maxPending);
}

void TestPredictionAndReconciliation() {
    RunPredictionScenario(false);
    RunPredictionScenario(true);

    // An acknowledgement of an input that was never sent is refused and changes nothing.
    ClientPrediction<Vec2, MoveInput> prediction(
        [](const Vec2& state, const MoveInput& input, float dt) { return StepMove(state, input, dt, false); },
        [](const Vec2& a, const Vec2& b) { return static_cast<double>(std::fabs(a.x - b.x)); }, Vec2{}, 1.0F, 0.1, 4U);
    for (int index = 0; index < 4; ++index) {
        Require(prediction.Predict(MoveInput{ 1.0F, 0.0F }) != 0U, "an input within the limit was refused");
    }
    Require(prediction.Predict(MoveInput{ 1.0F, 0.0F }) == 0U && prediction.GetStats().refusedInputs == 1U, "the history limit was not enforced");
    const Vec2 before = prediction.Current();
    Require(prediction.Reconcile(Vec2{ 99.0F, 99.0F }, 40U) == 0.0 && prediction.GetStats().rejectedAcks == 1U && prediction.Current().x == before.x,
        "an acknowledgement of an unsent input changed the state");
    Require(prediction.Reconcile(Vec2{ 0.0F, 0.0F }, 2U) > 0.0 && prediction.Pending() == 2U, "reconciliation did not drop the processed inputs");
}

void TestSnapshotInterpolation() {
    SnapshotInterpolator<Vec2> interpolator(
        [](const Vec2& a, const Vec2& b, double t) { return Vec2{ static_cast<float>(a.x + (b.x - a.x) * t), static_cast<float>(a.y + (b.y - a.y) * t) }; }, 0.1, 4U);
    Require(!interpolator.Sample(1.0).has_value(), "an empty interpolator returned a state");
    Require(interpolator.Add(1.0, Vec2{ 0.0F, 0.0F }) && interpolator.Add(1.1, Vec2{ 10.0F, 0.0F }) && interpolator.Add(1.2, Vec2{ 20.0F, 0.0F }), "snapshots were refused");
    Require(!interpolator.Add(1.1, Vec2{ 99.0F, 0.0F }), "an old snapshot was accepted");
    Require(std::fabs(interpolator.Sample(1.25)->x - 15.0F) < 1e-4F, "the state between two snapshots is wrong");
    Require(interpolator.Sample(0.5)->x == 0.0F, "a time before the first snapshot should hold the first");
    Require(interpolator.Sample(5.0)->x == 20.0F, "a time after the last snapshot extrapolated");
    for (int index = 0; index < 20; ++index) {
        static_cast<void>(interpolator.Add(2.0 + index * 0.1, Vec2{ static_cast<float>(index), 0.0F }));
    }
    Require(interpolator.Size() == 4U, "the snapshot history is not bounded");
}

void TestRegistryMismatchIsRefused() {
    Rig rig;
    // A client built with another layout of Facing.
    rig.AddPeer(1U, 9101U, 0U);
    auto otherComponents = std::make_unique<ReplicationRegistry>();
    std::string error;
    auto position = std::make_unique<StructCodec<Position>>("Position");
    position->Float(&Position::x, -500.0F, 500.0F, 16U).Float(&Position::y, -500.0F, 500.0F, 16U).Float(&Position::z, -500.0F, 500.0F, 16U);
    Require(otherComponents->Add(std::move(position), error), error);
    Rig::Peer& peer = *rig.peers[0];
    peer.replication = std::make_unique<ReplicationClient>(*peer.client, *peer.world, *otherComponents, *rig.rpcs, ReplicationConfig{});
    static_cast<void>(rig.Spawn(0.0F, 0.0F, 1U));
    rig.Run(0U, 5000U, [&] { return peer.replication->Stats().helloMismatch > 0U; });
    Require(peer.replication->Stats().helloMismatch == 1U && !peer.replication->Ready(), "a client with another layout was accepted");
    rig.Run(5000U, 2000U, [] { return false; });
    Require(peer.replication->ReplicaCount() == 0U && rig.server->ReadyClients() == 0U, "state was sent to a client with another layout");
}

void TestRelevance() {
    ReplicationConfig config;
    config.visibilityRadius = 100.0F;
    config.gridPlane = GridPlane::XY;
    Rig rig({}, config);
    rig.AddPeer(1U, 9101U, 0U);
    std::vector<kb::ecs::Entity> entities;
    std::vector<NetworkId> ids;
    for (int index = 0; index < 3; ++index) {
        NetworkId id = 0U;
        entities.push_back(rig.Spawn(static_cast<float>(index) * 200.0F, 0.0F, index == 0 ? 1U : 0U, &id));
        ids.push_back(id);
    }
    // The viewer stands at x = 0 and sees 100 units; the entities are at 0, 200 and 400.
    const auto visibleNow = [&](NetworkId id) { return id == ids[0]; };
    std::uint64_t now = rig.Run(0U, 10'000U, [&] { return rig.AllReady() && rig.Converged(*rig.peers[0], visibleNow); });
    Require(rig.Converged(*rig.peers[0], visibleNow) && rig.peers[0]->replication->ReplicaCount() == 1U, "only the near entity should be replicated");

    // Entity 2 walks in (to x = 90) and then to the edge (x = 110, inside the hysteresis band): it stays.
    rig.Move(entities[1], Position{ 90.0F, 0.0F, 0.0F });
    bool nearVisible = true;
    const auto visibleTwo = [&](NetworkId id) { return id == ids[0] || id == ids[1]; };
    now = rig.Run(now, 10'000U, [&] { return rig.Converged(*rig.peers[0], visibleTwo); });
    Require(rig.Converged(*rig.peers[0], visibleTwo), "an entity that came into range was not replicated");
    const std::uint64_t spawnsBefore = rig.peers[0]->replication->Stats().spawnsApplied;
    for (int step = 0; step < 20; ++step) {
        rig.Move(entities[1], Position{ step % 2 == 0 ? 110.0F : 105.0F, 0.0F, 0.0F });
        now = rig.Run(now, 60U, [] { return false; });
        nearVisible = nearVisible && rig.peers[0]->replication->Find(ids[1]).IsValid();
    }
    Require(nearVisible && rig.peers[0]->replication->Stats().despawnsApplied == 0U && rig.peers[0]->replication->Stats().spawnsApplied == spawnsBefore,
        "an entity at the edge flickered");
    // Out of the band it goes.
    rig.Move(entities[1], Position{ 150.0F, 0.0F, 0.0F });
    now = rig.Run(now, 10'000U, [&] { return rig.Converged(*rig.peers[0], visibleNow); });
    Require(rig.Converged(*rig.peers[0], visibleNow) && rig.peers[0]->replication->Stats().despawnsApplied == 1U, "an entity out of range was not despawned");
    Require(rig.server->ReplicatedCount() == 3U, "relevance removed an entity from the server");
}

void ConfigureHealthAudience(Rig& rig, ReplicationAudience audience) {
    rig.server.reset();
    rig.components = std::make_unique<ReplicationRegistry>();
    auto codec = std::make_unique<StructCodec<Health>>("Health");
    codec->UInt(&Health::hp, 1000U).Bool(&Health::alive);
    std::string error;
    Require(rig.components->Add(std::move(codec), audience, error), error);
    rig.server = std::make_unique<ReplicationServer>(*rig.host, *rig.world, *rig.components, *rig.rpcs, rig.config_);
}

kb::network::ReplicationDeclarations HealthDeclarations() {
    kb::network::ReplicationDeclarations declarations;
    declarations.fields.push_back({std::string(kb::scene::CombatantComponent::StableId), "health", ReplicationAudience::Owner, 0.0F, 100.0F, 8U});
    return declarations;
}

void TestDeclarationsRoundTrip() {
    auto declarations = HealthDeclarations();
    declarations.fields.push_back({std::string(kb::scene::CombatantComponent::StableId), "maximumHealth", ReplicationAudience::Everyone, 0.0F, 1000.0F, 12U});
    declarations.variables.push_back({"score", kb::network::ReplicatedVariableDecl::Type::Int, 0.0F, 10000.0F, 0U, ReplicationAudience::Everyone});
    declarations.variables.push_back({"ready", kb::network::ReplicatedVariableDecl::Type::Bool, 0.0F, 1.0F, 1U, ReplicationAudience::InitialOnly});
    const auto text = kb::network::WriteReplicationDeclarations(declarations);
    kb::network::ReplicationDeclarations parsed;
    std::string error;
    Require(kb::network::ParseReplicationDeclarations(text, parsed, error), error);
    Require(kb::network::WriteReplicationDeclarations(parsed) == text, "declarations round trip changed the canonical text");
    kb::scene::Scene scene;
    auto first = BuildReplicationRegistry(scene.Runtime().EcsWorld(), declarations, 1000.0F, error);
    std::reverse(declarations.fields.begin(), declarations.fields.end());
    auto second = BuildReplicationRegistry(scene.Runtime().EcsWorld(), declarations, 1000.0F, error);
    Require(first && second && first->Fingerprint() == second->Fingerprint(), "field order changed the registry fingerprint");
}

void TestDeclarationsRejectBadRange() {
    kb::scene::Scene scene;
    std::string error;
    const auto reject = [&](auto mutate, std::string_view name) {
        auto declarations = HealthDeclarations();
        mutate(declarations);
        Require(!BuildReplicationRegistry(scene.Runtime().EcsWorld(), declarations, 1000.0F, error), "invalid declaration was accepted");
        Require(error.find(name) != std::string::npos, "declaration error omitted the field: " + error);
    };
    reject([](auto& d) { d.fields[0].minimum = 100.0F; }, "health");
    reject([](auto& d) { d.fields[0].bits = 25U; }, "health");
    reject([](auto& d) { d.fields[0].bits = 0U; }, "health");
    reject([](auto& d) { d.fields.push_back(d.fields[0]); }, "health");
    reject([](auto& d) { d.fields[0].component = "missing.component"; }, "missing.component.health");
    reject([](auto& d) { d.fields[0].field = "missingField"; }, "missingField");
    reject([](auto& d) { d.variables.push_back({"score", kb::network::ReplicatedVariableDecl::Type::Int, -16777218.0F, 100.0F}); }, "score");
    kb::network::ReplicationDeclarations parsed;
    auto tooMany = HealthDeclarations();
    for (int i = 0; i < 30; ++i) tooMany.fields.push_back({"component" + std::to_string(i), "field"});
    Require(!kb::network::ParseReplicationDeclarations(kb::network::WriteReplicationDeclarations(tooMany), parsed, error) && error.find("29 groups") != std::string::npos, "group limit was not enforced");
}

void TestUnknownFieldIsRefusedAtLoad() {
    const auto root = std::filesystem::temp_directory_path() / "kb_replication_config_test";
    std::filesystem::create_directories(root / "Config");
    std::filesystem::create_directories(root / "Content");
    auto declarations = HealthDeclarations();
    declarations.fields[0].field = "unknownHealth";
    { std::ofstream output(root / "Config" / "Replication.json"); output << kb::network::WriteReplicationDeclarations(declarations); }
    kb::scene::Scene scene;
    Require(scene.Assets().Manager().Mounts().Mount("Game", root / "Content"), "test assets mount failed");
    std::string error;
    Require(!LoadReplicationDeclarations(scene, declarations, error) && error.find("unknownHealth") != std::string::npos, "unknown field was not refused at load: " + error);
    std::filesystem::remove(root / "Config" / "Replication.json");
    Require(LoadReplicationDeclarations(scene, declarations, error) && declarations.fields.empty(), "missing file did not use transform-only declarations");
}

void TestClientWithDifferentDeclarationsIsRefused() {
    kb::scene::Scene scene;
    Rig rig;
    std::string error;
    rig.server.reset();
    rig.components = BuildReplicationRegistry(scene.Runtime().EcsWorld(), HealthDeclarations(), 1000.0F, error);
    Require(rig.components != nullptr, error);
    rig.server = std::make_unique<ReplicationServer>(*rig.host, *rig.world, *rig.components, *rig.rpcs);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    auto different = HealthDeclarations();
    different.fields[0].bits = 9U;
    auto clientComponents = BuildReplicationRegistry(scene.Runtime().EcsWorld(), different, 1000.0F, error);
    peer.replication = std::make_unique<ReplicationClient>(*peer.client, *peer.world, *clientComponents, *rig.rpcs);
    rig.Run(0U, 1000U, [] { return false; });
    Require(!peer.replication->Ready() && rig.server->ReadyClients() == 0U, "different declarations were accepted");
    bool named = false;
    for (const auto& diagnostic : peer.replication->Diagnostics().Entries()) named |= std::string_view(diagnostic.detail).find("helloMismatch") != std::string::npos;
    Require(named, "declaration mismatch was not named");
}

void TestReflectedFieldsRoundTrip() {
    kb::scene::Scene scene;
    Rig rig;
    std::string error;
    rig.server.reset();
    rig.components = BuildReplicationRegistry(scene.Runtime().EcsWorld(), HealthDeclarations(), 1000.0F, error);
    Require(rig.components != nullptr, error);
    rig.server = std::make_unique<ReplicationServer>(*rig.host, *rig.world, *rig.components, *rig.rpcs);
    auto& owner = rig.AddPeer(1U, 9101U, 0U);
    auto& other = rig.AddPeer(2U, 9102U, 0U);
    const auto entity = rig.world->CreateEntity();
    kb::scene::CombatantComponent combatant;
    combatant.health = 37.0F;
    rig.world->Set(entity, combatant);
    Require(rig.server->Replicate(entity, 1U) == ReplicateResult::Ok, "combatant not replicated");
    const auto id = rig.server->NetworkIdOf(entity);
    rig.Run(0U, 1000U, [] { return false; });
    const auto replica = owner.replication->Find(id);
    Require(replica.IsValid(), "combatant replica missing; ready=" + std::to_string(owner.replication->Ready()) + " malformed=" + std::to_string(owner.replication->Stats().rejectedMalformed));
    const auto* value = owner.world->TryGet<kb::scene::CombatantComponent>(replica);
    Require(value && std::abs(value->health - 37.0F) <= 0.4F, "reflected health did not round trip");
    Require(!other.world->Has<kb::scene::CombatantComponent>(other.replication->Find(id)), "owner-only native component leaked");
    combatant.health = 62.0F;
    rig.world->Set(entity, combatant);
    rig.Run(1000U, 1000U, [] { return false; });
    Require(std::abs(owner.world->TryGet<kb::scene::CombatantComponent>(replica)->health - 62.0F) <= 0.4F, "reflected health change was not observed");
}

void TestReflectedCodecClampsOutOfRange() {
    kb::scene::Scene scene;
    auto& world = scene.Runtime().EcsWorld();
    std::string error;
    auto registry = BuildReplicationRegistry(world, HealthDeclarations(), 1000.0F, error);
    Require(registry != nullptr, error);
    const auto entity = world.CreateEntity();
    const auto replica = world.CreateEntity();
    for (const float health : {-50.0F, 150.0F}) {
        kb::scene::CombatantComponent component;
        component.health = health;
        world.Set(entity, component);
        std::vector<std::uint8_t> bytes(registry->At(3).EncodedSize());
        Require(registry->At(3).Capture(world, entity, bytes), "native field capture failed: " + registry->At(3).Describe());
        Require(registry->At(3).Validate(bytes), "native field validation failed");
        registry->At(3).Apply(world, replica, bytes);
        Require(world.TryGet<kb::scene::CombatantComponent>(replica)->health == std::clamp(health, 0.0F, 100.0F), "native field did not clamp");
    }
}

void CheckAudience(ReplicationAudience audience, bool transfer = false) {
    Rig rig;
    ConfigureHealthAudience(rig, audience);
    for (std::uint64_t id = 1; id <= 3U; ++id) rig.AddPeer(id, static_cast<std::uint16_t>(9100U + id), 0U);
    NetworkId id;
    const auto entity = rig.Spawn(0.0F, 0.0F, 1U, &id);
    rig.world->Set(entity, Health{37U, false});
    auto now = rig.Run(0U, 1000U, [] { return false; });
    Require(rig.AllReady(), "audience clients did not connect");
    const auto check = [&](std::uint64_t owner, std::uint32_t value) {
        for (auto& peer : rig.peers) {
            const auto replica = peer->replication->Find(id);
            Require(replica.IsValid(), "audience test lost an entity");
            const auto* health = peer->world->TryGet<Health>(replica);
            const bool allowed = audience == ReplicationAudience::Everyone || audience == ReplicationAudience::InitialOnly ||
                (audience == ReplicationAudience::Owner ? peer->id == owner : peer->id != owner);
            Require(allowed ? health != nullptr && health->hp == value : health == nullptr, "a component reached the wrong audience or retained revoked data");
        }
    };
    check(1U, 37U);
    if (transfer) {
        Require(rig.server->SetOwner(id, 2U), "ownership transfer failed");
        now = rig.Run(now, 1000U, [] { return false; });
        check(2U, 37U);
    }
    rig.world->Set(entity, Health{88U, false});
    rig.Run(now, 1000U, [] { return false; });
    check(transfer ? 2U : 1U, audience == ReplicationAudience::InitialOnly ? 37U : 88U);
    if (audience == ReplicationAudience::InitialOnly) Require(rig.server->Stats().stateMessagesSent == 0U, "InitialOnly was sent in state updates");
}
void TestOwnerOnlyGroupReachesOnlyOwner() { CheckAudience(ReplicationAudience::Owner); }
void TestOthersGroupSkipsOwner() { CheckAudience(ReplicationAudience::Others); }
void TestOwnershipChangeMovesOwnerOnlyData() { CheckAudience(ReplicationAudience::Owner, true); CheckAudience(ReplicationAudience::Others, true); }
void TestInitialOnlyGroupIsSentOnce() { CheckAudience(ReplicationAudience::InitialOnly); }

void TestRegistryFingerprintCoversAudience() {
    auto foreign = std::make_unique<ReplicationRegistry>();
    auto codec = std::make_unique<StructCodec<Health>>("Health");
    codec->UInt(&Health::hp, 1000U).Bool(&Health::alive);
    std::string error;
    Require(foreign->Add(std::move(codec), ReplicationAudience::Everyone, error), error);
    Rig rig;
    ConfigureHealthAudience(rig, ReplicationAudience::Owner);
    Require(foreign->Fingerprint() != rig.components->Fingerprint(), "audience is absent from the registry fingerprint");
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    peer.replication = std::make_unique<ReplicationClient>(*peer.client, *peer.world, *foreign, *rig.rpcs);
    rig.Run(0U, 2000U, [] { return false; });
    Require(peer.replication->Stats().helloMismatch == 1U && !peer.replication->Ready(), "different audiences were accepted");
    bool named = false;
    for (const auto& entry : peer.replication->Diagnostics().Entries()) named = named || std::string_view(entry.detail) == "helloMismatch";
    Require(named, "audience mismatch was not named");
}

void TestThirtyTwoGroupsAndRemovalRoundTrip() {
    Rig rig;
    rig.server.reset();
    rig.components = std::make_unique<ReplicationRegistry>();
    std::string error;
    for (std::size_t index = 0; index < 32U; ++index) {
        auto codec = std::make_unique<StructCodec<Health>>("Health" + std::to_string(index));
        codec->UInt(&Health::hp, 1000U);
        Require(rig.components->Add(std::move(codec), error), error);
    }
    rig.server = std::make_unique<ReplicationServer>(*rig.host, *rig.world, *rig.components, *rig.rpcs);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    NetworkId id;
    const auto entity = rig.Spawn(0.0F, 0.0F, 0U, &id);
    auto now = rig.Run(0U, 1000U, [] { return false; });
    rig.world->Set(entity, Health{77U, false});
    now = rig.Run(now, 1000U, [] { return false; });
    const auto replica = peer.replication->Find(id);
    Require(replica.IsValid() && peer.world->TryGet<Health>(replica)->hp == 77U, "32-bit change mask did not round trip");
    rig.world->Remove<Health>(entity);
    rig.Run(now, 1000U, [] { return false; });
    Require(!peer.world->Has<Health>(replica), "32-bit removal mask did not round trip");
}

void TestSceneWritesMarkReplicationDirty() {
    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Sample"}));
    const auto path = std::filesystem::temp_directory_path() / ("21kb_network_scene_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".21kbscene");
    Require(kb::scene::SceneDocumentService::Save(scene, path, "Sample") && kb::scene::SceneDocumentService::LoadFileIntoScene(scene, path), "scene round trip failed");
    std::filesystem::remove(path);
    auto& world = scene.Runtime().EcsWorld();
    kb::ecs::Entity loaded;
    world.ForEach<kb::scene::TransformComponent>([](kb::ecs::Entity entity, const kb::scene::TransformComponent&, void* context) { *static_cast<kb::ecs::Entity*>(context) = entity; }, &loaded);
    std::size_t appeared = 0U;
    const auto observer = world.ObserveComponent<kb::scene::TransformComponent>(kb::ecs::ComponentEventKind::Added,
        [](kb::ecs::Entity, kb::ecs::ComponentEventKind, const kb::scene::TransformComponent*, void* context) { ++*static_cast<std::size_t*>(context); }, &appeared, true);
    std::printf("  loaded scene: %zu existing transforms yielded, mirror=%d\n", appeared, world.MirrorsValueWrites(world.Component<kb::scene::TransformComponent>()));
    world.DestroyObserver(observer);
    LoopbackNetwork network;
    auto port = network.CreateTransport(Endpoint::Loopback(9000));
    StaticKeyTable keys;
    Host host(*port, {}, keys);
    std::string registryError;
    auto components = BuildReplicationRegistry(world, {}, 1000.0F, registryError);
    RpcRegistry rpcs;
    ReplicationServer server(host, world, *components, rpcs);
    ScriptNetworkRuntime runtime(ScriptNetworkRuntime::Role::Server, scene, {});
    runtime.BindServer(server);
    Require(server.Replicate(loaded) == ReplicateResult::Ok, "scene replication failed");
    const auto before = server.Stats().capturedEntities;
    auto transform = *scene.Transforms().TryGet(loaded);
    transform.localPosition.x = 10.0F;
    scene.Transforms().Set(loaded, transform);
    server.Tick(1U, 0U);
    Require(server.Stats().capturedEntities == before + 1U, "scene transform write did not mark replication dirty");
}

void TestTickCostScalesWithChangedNotTotal() {
    Rig rig;
    std::vector<kb::ecs::Entity> moving;
    for (std::size_t index = 0; index < 100'100U; ++index) {
        const auto entity = rig.Spawn(0.0F, 0.0F, 0U);
        if (index < 100U) moving.push_back(entity);
    }
    rig.server->Tick(1U, 0U);
    const auto capacity = rig.server->DirtyCapacity();
    const auto before = rig.server->Stats().capturedEntities;
    for (const auto entity : moving) {
        rig.world->Set(entity, Position{1.0F, 2.0F, 3.0F});
        rig.world->Set(entity, Position{4.0F, 5.0F, 6.0F});
    }
    rig.server->Tick(2U, 33U);
    const auto captured = rig.server->Stats().capturedEntities - before;
    Require(captured == 100U && captured <= 120U && rig.server->DirtyCapacity() == capacity, "dirty capture scanned unchanged objects, duplicated work, or grew its buffer");
    rig.server->Tick(3U, 66U);
    Require(rig.server->Stats().capturedEntities == before + captured, "a clean tick captured objects");
    std::printf("  dirty capture: %llu of 100100 objects; capacity unchanged\n", static_cast<unsigned long long>(captured));
}

void TestRotationSmallestThreeRoundTrip() {
    kb::ecs::World world;
    const auto source = world.CreateEntity(), replica = world.CreateEntity();
    SceneTransformCodec codec(1000.0F, TransformGroup::Rotation);
    Require(codec.EncodedSize() == 4U, "rotation is not four bytes");
    Random random{0xAA0123456789ULL};
    float maximumError = 0.0F;
    for (std::size_t i = 0; i < 10'000U; ++i) {
        std::array<float, 4> q{};
        float sum = 0;
        for (auto& value : q) { value = static_cast<float>(static_cast<int>(random.Below(2'000'001U)) - 1'000'000) / 1'000'000.0F; sum += value * value; }
        if (sum < 1.0e-8F) { --i; continue; }
        for (auto& value : q) value /= std::sqrt(sum);
        kb::scene::TransformComponent transform;
        transform.localRotation = {q[0], q[1], q[2], q[3]};
        world.Set(source, transform);
        std::array<std::uint8_t, 4> bytes{};
        Require(codec.Capture(world, source, bytes) && codec.Validate(bytes), "rotation capture failed");
        codec.Apply(world, replica, bytes);
        const auto& decoded = world.TryGet<kb::scene::TransformComponent>(replica)->localRotation;
        const double dot = std::abs(static_cast<double>(q[0]) * decoded.x + static_cast<double>(q[1]) * decoded.y + static_cast<double>(q[2]) * decoded.z + static_cast<double>(q[3]) * decoded.w);
        const double qLength = std::sqrt(static_cast<double>(q[0])*q[0] + static_cast<double>(q[1])*q[1] + static_cast<double>(q[2])*q[2] + static_cast<double>(q[3])*q[3]);
        const double decodedLength = std::sqrt(static_cast<double>(decoded.x)*decoded.x + static_cast<double>(decoded.y)*decoded.y + static_cast<double>(decoded.z)*decoded.z + static_cast<double>(decoded.w)*decoded.w);
        const auto angle = static_cast<float>(2.0 * std::acos(std::clamp(dot / (qLength * decodedLength), 0.0, 1.0)) * 180.0 / 3.141592653589793);
        maximumError = std::max(maximumError, angle);
    }
    std::printf("  rotation: maximum error %.4f degrees over 10000 quaternions\n", maximumError);
    Require(maximumError <= 0.2F, "rotation error exceeds 0.2 degrees");
}

void TestPositionOnlyChangeCostsSixBytes() {
    Rig rig;
    rig.server.reset();
    std::string registryError;
    rig.components = BuildReplicationRegistry(*rig.world, {}, 1000.0F, registryError);
    rig.server = std::make_unique<ReplicationServer>(*rig.host, *rig.world, *rig.components, *rig.rpcs);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    const auto entity = rig.world->CreateEntity();
    rig.world->Set(entity, kb::scene::TransformComponent{});
    Require(rig.server->Replicate(entity) == ReplicateResult::Ok, "transform was not replicated");
    const auto id = rig.server->NetworkIdOf(entity);
    auto now = rig.Run(0U, 3000U, [&] { return rig.AllReady() && peer.replication->Find(id).IsValid(); });
    now = rig.Run(now, 200U, [] { return false; });
    rig.capturedToClient.clear();
    auto transform = *rig.world->TryGet<kb::scene::TransformComponent>(entity);
    transform.localPosition.x = 123.0F;
    rig.world->Set(entity, transform);
    rig.Run(now, 500U, [] { return false; });
    std::size_t messages = 0U;
    for (const auto& message : rig.capturedToClient) {
        if (message.data.empty() || message.data[0] != 0x10U) continue;
        Require(message.data.size() == 11U + 8U + 6U, "position change did not cost header plus six bytes");
        ByteReader reader(std::span<const std::uint8_t>(message.data).subspan(17U));
        Require(reader.U16() == 1U, "unchanged rotation or scale was sent");
        ++messages;
    }
    Require(messages > 0U, "no position state was sent");
}

void TestScaleSentOnceWhenUnchanged() {
    Rig rig;
    rig.server.reset();
    std::string registryError;
    rig.components = BuildReplicationRegistry(*rig.world, {}, 1000.0F, registryError);
    rig.server = std::make_unique<ReplicationServer>(*rig.host, *rig.world, *rig.components, *rig.rpcs);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    const auto entity = rig.world->CreateEntity();
    kb::scene::TransformComponent transform;
    transform.localScale = {2.0F, 3.0F, 4.0F};
    rig.world->Set(entity, transform);
    Require(rig.server->Replicate(entity) == ReplicateResult::Ok, "transform was not replicated");
    rig.Run(0U, 2000U, [] { return false; });
    Require(rig.AllReady() && peer.replication->Stats().spawnsApplied == 1U && rig.server->Stats().stateMessagesSent == 0U,
        "unchanged transform groups were sent after Spawn");
}

void TestChangedFilterReportsNewAndMovedEntities() {
    kb::ecs::World world(kb::ecs::WorldConfig{ .reserveEntities = 100'100U, .mirrorEntitiesToBackend = false,
        .mirrorNativeComponentChangesToBackend = false, .trackEntityCatalog = false });
    std::vector<kb::ecs::Entity> moving;
    for (std::size_t index = 0; index < 100'100U; ++index) {
        const auto entity = world.CreateEntity();
        world.Set(entity, kb::scene::TransformComponent{});
        if (index < 100U) moving.push_back(entity);
    }
    kb::ecs::QueryFilter filter;
    filter.Changed(world.Component<kb::scene::TransformComponent>());
    auto query = world.CreateQuery<kb::scene::TransformComponent>(filter);
    struct Observation { std::size_t visited = 0U; bool movedFound = false; kb::ecs::Entity moved; };
    const auto visit = [](kb::ecs::Entity entity, const kb::scene::TransformComponent&, void* context) {
        auto& observed = *static_cast<Observation*>(context);
        ++observed.visited;
        observed.movedFound = observed.movedFound || entity == observed.moved;
    };
    Observation initial{0U, false, moving.front()};
    query.ForEach(visit, &initial);
    Require(initial.visited == 100'100U && initial.movedFound, "Changed filter did not report new transforms");
    Observation unchanged;
    query.ForEach(visit, &unchanged);
    Require(unchanged.visited == 0U, "Changed filter reported an unchanged archetype");
    for (const auto entity : moving) {
        auto transform = *world.TryGet<kb::scene::TransformComponent>(entity);
        transform.localPosition.x = 10.0F;
        world.Set(entity, transform);
    }
    Observation changed{0U, false, moving.front()};
    query.ForEach(visit, &changed);
    Require(changed.movedFound, "Changed filter did not report moved transforms");
    std::printf("  Changed filter: %zu new, %zu unchanged, %zu visited after 100 writes among 100100 transforms\n",
        initial.visited, unchanged.visited, changed.visited);
}

void TestGridVisibilityMatchesBruteForce() {
    ReplicationConfig config;
    config.visibilityRadius = 100.0F;
    config.gridPlane = GridPlane::XY;
    Rig rig({}, config);
    Random random{0x1234567890ABCDEFULL};
    std::vector<kb::ecs::Entity> entities;
    std::vector<NetworkId> ids;
    std::vector<Position> positions;
    for (std::size_t i = 0; i < 5000U; ++i) {
        const auto x = static_cast<float>(static_cast<int>(random.Below(4000U)) - 2000);
        const auto y = static_cast<float>(static_cast<int>(random.Below(4000U)) - 2000);
        NetworkId id;
        entities.push_back(rig.Spawn(x, y, i < 50U ? i + 1U : 0U, &id));
        ids.push_back(id);
        positions.push_back({x, y, 0.0F});
    }
    for (std::uint64_t client = 1; client <= 50U; ++client) rig.AddPeer(client, static_cast<std::uint16_t>(9100U + client), 0U);
    auto now = rig.Run(0U, 3000U, [] { return false; });
    Require(rig.AllReady(), "grid clients did not connect");
    rig.nextTickAt = UINT64_MAX;
    std::vector<std::set<NetworkId>> previous(50U);
    for (std::size_t client = 0; client < 50U; ++client) {
        const auto visible = rig.server->VisibleIds(client + 1U);
        previous[client].insert(visible.begin(), visible.end());
    }
    for (std::size_t step = 0; step < 200U; ++step) {
        for (std::size_t move = 0; move < 150U; ++move) {
            const auto index = move < 50U ? move : random.Below(5000U);
            positions[index].x += static_cast<float>(static_cast<int>(random.Below(21U)) - 10);
            positions[index].y += static_cast<float>(static_cast<int>(random.Below(21U)) - 10);
            rig.Move(entities[index], positions[index]);
        }
        rig.server->Tick(++rig.tick, now);
        for (std::size_t client = 0; client < 50U; ++client) {
            std::set<NetworkId> expected;
            for (std::size_t entity = 0; entity < ids.size(); ++entity) {
                const auto dx = positions[entity].x - positions[client].x, dy = positions[entity].y - positions[client].y;
                const auto radius = previous[client].contains(ids[entity]) ? 115.0F : 100.0F;
                if (entity == client || dx * dx + dy * dy <= radius * radius) expected.insert(ids[entity]);
            }
            const auto actual = rig.server->VisibleIds(client + 1U);
            Require(std::vector<NetworkId>(expected.begin(), expected.end()) == actual, "grid visibility differs from brute force");
            previous[client] = std::move(expected);
        }
        now = rig.Run(now, 40U, [] { return false; });
    }
}

void TestVisibilityCostIsIndependentOfWorldSize() {
    ReplicationConfig config;
    config.visibilityRadius = 100.0F;
    config.gridPlane = GridPlane::XY;
    Rig rig({}, config);
    for (std::size_t index = 0; index < 100'000U; ++index) {
        static_cast<void>(rig.Spawn(index <= 200U ? static_cast<float>(index) * 0.1F : 10'000.0F + static_cast<float>(index) * 20.0F,
            0.0F, index == 0U ? 1U : 0U));
    }
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    const auto now = rig.Run(0U, 3000U, [&] { return rig.AllReady() && peer.replication->ReplicaCount() == 201U; });
    Require(rig.AllReady() && peer.replication->ReplicaCount() == 201U, "nearby objects were not delivered");
    const auto before = rig.server->Stats().candidatesExamined;
    rig.server->Tick(++rig.tick, now);
    const auto examined = rig.server->Stats().candidatesExamined - before;
    Require(examined <= 1000U, "visibility examined the entire world");
    std::printf("  visibility: %llu candidates for 100000 objects\n", static_cast<unsigned long long>(examined));
}

void TestAlwaysRelevantAndOwnedAreVisibleEverywhere() {
    ReplicationConfig config;
    config.visibilityRadius = 10.0F;
    Rig rig({}, config);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    NetworkId owned = 0U, global = 0U, hidden = 0U;
    const auto viewer = rig.Spawn(0.0F, 0.0F, 1U, &owned);
    static_cast<void>(rig.Spawn(1000.0F, 0.0F, 0U, &global));
    static_cast<void>(rig.Spawn(1000.0F, 0.0F, 0U, &hidden));
    Require(rig.server->SetAlwaysRelevant(global, true), "cannot mark global object");
    const auto now = rig.Run(0U, 3000U, [&] { return rig.AllReady() && peer.replication->ReplicaCount() == 2U; });
    Require(peer.replication->Find(owned).IsValid() && peer.replication->Find(global).IsValid() && !peer.replication->Find(hidden).IsValid(),
        "global or owned visibility is incorrect");
    rig.world->Remove<kb::scene::TransformComponent>(viewer);
    rig.Run(now, 100U, [] { return false; });
    Require(peer.replication->Find(owned).IsValid() && peer.replication->Find(global).IsValid() && !peer.replication->Find(hidden).IsValid(),
        "a client without a viewer saw spatial objects or lost its own or global objects");
}

void TestMillionObjectsAreAcceptedAndWarned() {
    LoopbackNetwork network;
    auto port = network.CreateTransport(Endpoint::Loopback(9000));
    StaticKeyTable keys;
    Host host(*port, {}, keys);
    kb::ecs::World world(kb::ecs::WorldConfig{ .reserveEntities = 1'000'001U, .mirrorEntitiesToBackend = false,
        .mirrorNativeComponentChangesToBackend = false, .trackEntityCatalog = false });
    ReplicationRegistry components;
    RpcRegistry rpcs;
    ReplicationServer server(host, world, components, rpcs);
    std::size_t warnings = 0U;
    host.Diagnostics().SetSink([&](std::string_view message) { if (message.starts_with("ObjectCountWarning:")) ++warnings; });
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < 1'000'001U; ++i) {
        Require(server.Replicate(world.CreateEntity()) == ReplicateResult::Ok, "a network object was silently dropped");
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    Require(seconds < 6.0 && server.ReplicatedCount() == 1'000'001U && warnings == 1U, "million object acceptance or warning failed");
    server.Tick(1U, 0U);
    Require(warnings == 1U, "the object warning repeated without crossing its threshold");
    std::printf("  network objects: 1000001 accepted in %.3f s\n", seconds);
}

void TestNoSilentDropAbove4096() {
    Rig rig;
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    for (std::size_t index = 0; index < 5001U; ++index) static_cast<void>(rig.Spawn(0.0F, 0.0F, 0U));
    Require(rig.server->ReplicatedCount() == 5001U, "objects above 4096 were dropped");
    rig.Run(0U, 60'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(peer.replication->ReplicaCount() == 5001U && rig.AllConverged(), "replicas above 4096 were dropped");
}

void TestSlotReuseBumpsGeneration() {
    Rig rig;
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    NetworkId oldId = 0U;
    const auto oldEntity = rig.Spawn(1.0F, 2.0F, 0U, &oldId);
    auto now = rig.Run(0U, 10'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    rig.world->Set(oldEntity, Position{99.0F, 0.0F, 0.0F});
    now = rig.Run(now, 500U, [] { return false; });
    std::vector<std::uint8_t> oldState;
    for (const auto& message : rig.capturedToClient) if (!message.data.empty() && message.data[0] == 0x10U) oldState = message.data;
    Require(!oldState.empty() && rig.server->Unreplicate(oldId), "no old state or unreplication failed");
    NetworkId newId = 0U;
    static_cast<void>(rig.Spawn(20.0F, 30.0F, 0U, &newId));
    Require(NetIndex(oldId) == NetIndex(newId) && NetGeneration(newId) == NetGeneration(oldId) + 1U, "slot reuse did not advance the generation");
    now = rig.Run(now, 10'000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged() && !peer.replication->Find(oldId).IsValid(), "replacement generation did not replicate");
    const auto replica = peer.replication->Find(newId);
    const auto before = *peer.world->TryGet<Position>(replica);
    peer.replication->Flush();
    const auto stale = peer.replication->Stats().entriesStale;
    const auto acks = peer.replication->Stats().acksSent;
    peer.replication->HandleMessage(1U, oldState);
    peer.replication->Flush();
    const auto after = *peer.world->TryGet<Position>(replica);
    Require(peer.replication->Stats().entriesStale > stale && peer.replication->Stats().acksSent == acks && before.x == after.x,
        "an old generation changed the replacement or was acknowledged");
}

void TestLimits() {
    Rig rig;
    const auto first = rig.Spawn(0.0F, 0.0F, 0U);
    Require(rig.server->Replicate(first) == ReplicateResult::AlreadyReplicated, "duplicate replication was not identified");
    const auto dead = rig.world->CreateEntity();
    rig.world->DestroyEntity(dead);
    Require(rig.server->Replicate(dead) == ReplicateResult::NotAlive, "dead entity was replicated");

    // A byte rate limit spreads a big change over several ticks and still converges.
    ReplicationConfig slow;
    slow.requestedBytesPerSecond = slow.minClientRate = slow.maxClientRate = 3600U;
    slow.maxUpdateBytes = 120U;
    Rig limited({}, slow);
    limited.AddPeer(1U, 9101U, 0U);
    std::vector<kb::ecs::Entity> entities;
    for (int index = 0; index < 50; ++index) {
        entities.push_back(limited.Spawn(0.0F, 0.0F, 0U));
    }
    std::uint64_t now = limited.Run(0U, 60'000U, [&] { return limited.AllReady() && limited.AllConverged(); });
    Require(limited.AllConverged(), "a bandwidth limited server did not converge on the initial state");
    for (const kb::ecs::Entity entity : entities) {
        limited.world->Set(entity, Position{ 123.0F, -77.0F, 5.0F });
    }
    const std::uint64_t messagesBefore = limited.server->Stats().stateMessagesSent;
    now = limited.Run(now, 60'000U, [&] { return limited.AllConverged(); });
    Require(limited.AllConverged() && limited.server->Stats().stateMessagesSent - messagesBefore >= 5U, "the per tick limit did not spread the change");
}

void TestRateNeverExceedsClientRequest() {
    ReplicationConfig config;
    config.requestedBytesPerSecond = 32768U;
    Rig rig({}, config);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    std::vector<kb::ecs::Entity> entities;
    for (int i = 0; i < 1000; ++i) entities.push_back(rig.Spawn(0.0F, 0.0F, 0U));
    auto now = rig.Run(0U, 60'000U, [&] { return peer.replication->ReplicaCount() == entities.size(); });
    Require(peer.replication->ReplicaCount() == entities.size(), "rate test did not spawn all objects");
    Require(rig.server->ClientRate(1U) == config.requestedBytesPerSecond, "HelloAck did not negotiate the requested rate");
    rig.capturedToClient.clear();
    const auto start = now;
    for (; now < start + 10'000U; now += 10U) {
        if (now % 30U == 0U) for (const auto entity : entities) rig.Move(entity, {static_cast<float>((now / 30U) % 100U), 0.0F, 0.0F});
        rig.Step(now);
    }
    std::uint64_t bytes = 0U;
    for (const auto& message : rig.capturedToClient) if (!message.data.empty() && message.data[0] == 0x10U) bytes += message.data.size() + Session::MessageOverhead(false);
    Require(static_cast<double>(bytes) <= config.requestedBytesPerSecond * 10.0 * 1.05, "replication exceeded the requested byte rate");
    std::printf("  rate: %llu bytes over 10 s, request=%u B/s\n", static_cast<unsigned long long>(bytes), config.requestedBytesPerSecond);
}

void TestNearObjectsKeepFullRateUnderBudget() {
    constexpr std::size_t nearCount = 50U;
    ReplicationConfig config;
    config.visibilityRadius = 100.0F;
    config.requestedBytesPerSecond = 32768U;
    Rig rig({}, config);
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    static_cast<void>(rig.Spawn(0.0F, 0.0F, 1U));
    std::vector<kb::ecs::Entity> entities;
    std::vector<NetworkId> ids;
    for (std::size_t i = 0; i < nearCount + 10U; ++i) {
        NetworkId id;
        entities.push_back(rig.Spawn(i < nearCount ? 1.0F : 80.0F, 0.0F, 0U, &id));
        ids.push_back(id);
    }
    auto now = rig.Run(0U, 20'000U, [&] { return peer.replication->ReplicaCount() == entities.size() + 1U; });
    Require(peer.replication->ReplicaCount() == entities.size() + 1U, "priority test replicas missing");
    std::vector<std::uint32_t> updates(ids.size());
    std::vector<std::uint64_t> latest(ids.size(), now), maximumGap(ids.size());
    peer.replication->SetStateCallback([&](NetworkId id, kb::ecs::Entity, std::uint32_t) {
        const auto found = std::ranges::find(ids, id);
        if (found == ids.end()) return;
        const auto index = static_cast<std::size_t>(found - ids.begin());
        ++updates[index];
        maximumGap[index] = std::max(maximumGap[index], now - latest[index]);
        latest[index] = now;
    });
    const auto start = now;
    for (; now < start + 10'000U; now += 10U) {
        if (now % 30U == 0U) for (std::size_t i = 0; i < entities.size(); ++i) rig.Move(entities[i], {i < nearCount ? 1.0F : 80.0F, static_cast<float>((now / 30U) % 10U) * 0.1F, 0.0F});
        rig.Step(now);
    }
    for (std::size_t i = 0; i < ids.size(); ++i) {
        maximumGap[i] = std::max(maximumGap[i], now - latest[i]);
        Require(maximumGap[i] <= 2000U && updates[i] != 0U, "priority queue starved object " + std::to_string(i) + ": gap=" + std::to_string(maximumGap[i]) + " updates=" + std::to_string(updates[i]));
        Require(updates[i] >= (i < nearCount ? 280U : 70U), "priority rate target was missed");
    }
    const auto nearMinimum = *std::min_element(updates.begin(), updates.begin() + nearCount);
    const auto farMinimum = *std::min_element(updates.begin() + nearCount, updates.end());
    std::printf("  priorities: rate=%u B/s, minimum near=%.1f Hz far=%.1f Hz, gap=%llu ms\n", config.requestedBytesPerSecond, nearMinimum / 10.0, farMinimum / 10.0, static_cast<unsigned long long>(*std::ranges::max_element(maximumGap)));
}

void TestRateBacksOffUnderLossAndRecovers() {
    Rig rig;
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    std::vector<kb::ecs::Entity> entities;
    for (int i = 0; i < 100; ++i) entities.push_back(rig.Spawn(0.0F, 0.0F, 0U));
    auto now = rig.Run(0U, 5000U, [&] { return peer.replication->ReplicaCount() == entities.size(); });
    Require(peer.replication->ReplicaCount() == entities.size(), "loss test replicas missing");
    LinkConditions loss;
    loss.simulation.lossPermille = 300U;
    loss.simulation.seed = 47U;
    rig.network.SetConditions(loss);
    const auto drive = [&](std::uint64_t duration) {
        const auto end = now + duration;
        for (; now < end; now += 10U) {
            if (now % 30U == 0U) for (const auto entity : entities) rig.Move(entity, {static_cast<float>((now / 30U) % 100U), 0.0F, 0.0F});
            rig.Step(now);
        }
    };
    drive(5000U);
    const auto reduced = rig.server->RateScale(1U);
    Require(reduced <= 0.5F, "30 percent loss did not reduce the send rate within five seconds: " + std::to_string(reduced));
    rig.network.SetConditions({});
    drive(20'000U);
    Require(rig.server->RateScale(1U) >= 0.9F, "send rate did not recover after twenty clean seconds");
    std::printf("  AIMD: loss scale=%.2f recovered=%.2f\n", reduced, rig.server->RateScale(1U));
}

void TestCircuitBreakerStopsStateWhenNoAcks() {
    Rig rig;
    auto& peer = rig.AddPeer(1U, 9101U, 0U);
    const auto entity = rig.Spawn(0.0F, 0.0F, 0U);
    auto now = rig.Run(0U, 5000U, [&] { return peer.replication->ReplicaCount() == 1U; });
    rig.flushAcks = false;
    const auto drive = [&](std::uint64_t duration) {
        const auto end = now + duration;
        for (; now < end; now += 10U) { rig.Move(entity, {static_cast<float>(now % 100U), 0.0F, 0.0F}); rig.Step(now); }
    };
    drive(6000U);
    const auto before = rig.server->Stats().stateMessagesSent;
    drive(2000U);
    Require(before != 0U && rig.server->Stats().stateMessagesSent == before, "no-ack circuit breaker continued sending state");
    bool named = false;
    for (const auto& diagnostic : rig.host->Diagnostics().Entries()) named |= diagnostic.code == DiagCode::RateLimited && std::string_view(diagnostic.detail).find("player=1") != std::string_view::npos;
    Require(named, "no-ack circuit breaker omitted the player diagnostic");
}


// A change that does not fit in one state message is sent in parts, the rest follows; a component that alone is too big is named.
void TestLargeEntityIsSplitAcrossEntries() {
    const auto oversizeReports = [](const Rig& rig) {
        std::uint64_t reported = 0U;
        for (const DiagEntry& entry : rig.host->Diagnostics().Entries()) {
            if (entry.code == DiagCode::OversizeRecord) reported += entry.count;
        }
        return reported;
    };
    ReplicationConfig config;
    config.maxUpdateBytes = 27U; // an entry with all three components needs 29, each fits alone
    Rig rig({}, config);
    rig.AddPeer(1U, 9101U, 0U);
    const kb::ecs::Entity entity = rig.Spawn(1.0F, 2.0F, 0U);
    std::uint64_t now = rig.Run(0U, 10'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllConverged(), "the entity did not replicate");
    rig.capturedToClient.clear();
    rig.world->Set(entity, Position{ 100.0F, -50.0F, 7.0F });
    rig.world->Set(entity, Health{ 321U, false });
    rig.world->Set(entity, Facing{ 200.0F });
    now = rig.Run(now, 10'000U, [&] { return rig.AllConverged(); });
    Require(rig.AllConverged() && oversizeReports(rig) == 0U, "a change over the message size was not split across messages: " + rig.why);

    std::map<std::uint32_t, std::uint16_t> masksByTick;
    for (const auto& captured : rig.capturedToClient) {
        if (captured.data.empty() || captured.data[0] != 0x10U) continue;
        ByteReader reader(std::span<const std::uint8_t>(captured.data).subspan(1));
        const auto tick = reader.U32();
        static_cast<void>(reader.U32());
        const auto count = reader.U16();
        for (std::uint16_t entry = 0; entry < count; ++entry) {
            static_cast<void>(reader.U32());
            static_cast<void>(reader.U8());
            const auto flags = reader.U8();
            const auto mask = reader.U16();
            if ((flags & 1U) != 0U) static_cast<void>(reader.U16());
            Require(mask != 0U && (mask & (mask - 1U)) == 0U, "a split entry contains more than one component");
            masksByTick[tick] = static_cast<std::uint16_t>(masksByTick[tick] | mask);
            for (std::size_t component = 0; component < rig.components->Count(); ++component) {
                if ((mask & (1U << component)) != 0U) static_cast<void>(reader.Bytes(rig.components->At(component).EncodedSize()));
            }
        }
        Require(reader.Ok() && reader.Remaining() == 0U, "invalid split message");
    }
    Require(std::ranges::any_of(masksByTick, [](const auto& item) { return item.second == 7U; }),
        "the split did not send all components in one tick");

    ReplicationConfig tight;
    tight.maxUpdateBytes = 24U; // Position (6 bytes) cannot be sent at all
    Rig small({}, tight);
    small.AddPeer(1U, 9101U, 0U);
    const kb::ecs::Entity other = small.Spawn(1.0F, 2.0F, 0U);
    now = small.Run(0U, 5'000U, [&] { return small.AllReady() && small.AllConverged(); });
    now = small.Run(now, 100U, [] { return false; });
    small.world->Set(other, Health{ 77U, true });
    small.world->Set(other, Position{ 90.0F, 90.0F, 90.0F });
    small.Run(now, 5'000U, [&] { return false; });
    const kb::ecs::Entity replica = small.peers[0]->replication->Find(1U);
    const Health* health = replica.IsValid() ? small.peers[0]->world->TryGet<Health>(replica) : nullptr;
    Require(health != nullptr && health->hp == 77U, "a component that fits was held back by one that does not");
    bool named = false;
    for (const DiagEntry& entry : small.host->Diagnostics().Entries()) {
        named = named || (entry.code == DiagCode::OversizeRecord && std::string_view(entry.detail).find("Position") != std::string_view::npos);
    }
    Require(named, "a component over the message size was not named");
}

void TestHostileReplicationMessages() {
    Rig rig(BadLink());
    Rig::Peer& alice = rig.AddPeer(1U, 9101U, 0U);
    rig.AddPeer(2U, 9102U, 0U);
    std::vector<kb::ecs::Entity> entities;
    for (int index = 0; index < 6; ++index) {
        entities.push_back(rig.Spawn(static_cast<float>(index), 0.0F, index % 2 == 0 ? 1U : 2U));
    }
    rig.server->SetRpcHandler("Heal", [&](std::uint64_t, std::span<const RpcValue> args) {
        const kb::ecs::Entity target = rig.server->EntityOf(std::get<NetworkId>(args[0]));
        Health health = *rig.world->TryGet<Health>(target);
        health.hp = std::min<std::uint32_t>(1000U, health.hp + std::get<std::uint32_t>(args[1]));
        rig.world->Set(target, health);
    });
    std::uint64_t now = rig.Run(0U, 30'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    for (int step = 0; step < 100; ++step) {
        for (const kb::ecs::Entity entity : entities) {
            rig.world->Set(entity, Position{ static_cast<float>(step), static_cast<float>(step % 7), 0.0F });
        }
        now = rig.Run(now, 30U, [] { return false; });
    }
    now = rig.Run(now, 30'000U, [&] { return rig.AllConverged(); });
    Require(!rig.capturedToClient.empty() && !rig.capturedToServer.empty(), "no genuine messages were captured");

    // 1. Mutated and random messages into a separate client replica (its own world; its acknowledgements go nowhere).
    {
        LoopbackNetwork sinkNetwork;
        auto sinkPort = sinkNetwork.CreateTransport(Endpoint::Loopback(9201U));
        Client sinkClient(*sinkPort, ClientConfig{}, 1U, MakeKey(1U), Endpoint::Loopback(9999U), 0U);
        kb::ecs::World fuzzWorld;
        ReplicationClient fuzzed(sinkClient, fuzzWorld, *rig.components, *rig.rpcs, ReplicationConfig{});
        // The genuine hello makes the client ready.
        for (const Rig::Captured& captured : rig.capturedToClient) {
            if (captured.peer == 1U && !captured.data.empty() && captured.data[0] == 1U) {
                fuzzed.HandleMessage(captured.channel, captured.data);
                break;
            }
        }
        Require(fuzzed.Ready(), "the fuzzed client did not accept the genuine hello");
        Random random{ 0xBADC0FFEE0DDF00DULL };
        for (std::size_t iteration = 0U; iteration < 60'000U; ++iteration) {
            std::vector<std::uint8_t> bytes;
            std::uint8_t channel = 0U;
            if (iteration % 4U == 0U) {
                bytes.resize(random.Below(400U));
                for (std::uint8_t& byte : bytes) {
                    byte = static_cast<std::uint8_t>(random.Next() & 0xFFU);
                }
                if (!bytes.empty() && random.Below(2U) == 0U) {
                    static constexpr std::uint8_t kTypes[] = { 1U, 2U, 3U, 4U, 5U, 0x10U, 0x11U, 0x12U };
                    bytes[0] = kTypes[random.Below(8U)];
                }
                channel = static_cast<std::uint8_t>(random.Below(3U));
            } else {
                const Rig::Captured& captured = rig.capturedToClient[random.Below(rig.capturedToClient.size())];
                bytes = captured.data;
                channel = captured.channel;
                const std::size_t flips = random.Below(4U);
                for (std::size_t flip = 0U; flip < flips && !bytes.empty(); ++flip) {
                    bytes[random.Below(bytes.size())] ^= static_cast<std::uint8_t>(1U << random.Below(8U));
                }
                if (random.Below(6U) == 0U) {
                    bytes.resize(random.Below(bytes.size() + 1U));
                }
            }
            fuzzed.HandleMessage(channel, bytes);
            if (iteration % 1000U == 0U) {
                fuzzed.Flush();
            }
        }
        for (const NetworkId id : fuzzed.ReplicaIds()) {
            Require(fuzzWorld.IsAlive(fuzzed.Find(id)), "a replica entity is dead but still registered");
        }
        Require(fuzzed.Stats().rejectedMalformed > 1000U, "the fuzz did not reach the parser");
    }

    // 2. Mutated and random messages into the server, as if sent by alice (id 1). The entities alice does not own must not change.
    {
        std::vector<Position> before;
        std::vector<Health> healthBefore;
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            before.push_back(*rig.world->TryGet<Position>(entities[index]));
            healthBefore.push_back(*rig.world->TryGet<Health>(entities[index]));
        }
        const std::size_t replicated = rig.server->ReplicatedCount();
        Random random{ 0xFEEDFACECAFEBEEFULL };
        for (std::size_t iteration = 0U; iteration < 60'000U; ++iteration) {
            std::vector<std::uint8_t> bytes;
            std::uint8_t channel = 0U;
            if (iteration % 4U == 0U) {
                bytes.resize(random.Below(400U));
                for (std::uint8_t& byte : bytes) {
                    byte = static_cast<std::uint8_t>(random.Next() & 0xFFU);
                }
                if (!bytes.empty() && random.Below(2U) == 0U) {
                    static constexpr std::uint8_t kTypes[] = { 1U, 2U, 3U, 4U, 5U, 6U, 0x10U, 0x11U, 0x12U };
                    bytes[0] = kTypes[random.Below(9U)];
                }
                channel = static_cast<std::uint8_t>(random.Below(3U));
            } else {
                const Rig::Captured& captured = rig.capturedToServer[random.Below(rig.capturedToServer.size())];
                bytes = captured.data;
                channel = captured.channel;
                const std::size_t flips = random.Below(4U);
                for (std::size_t flip = 0U; flip < flips && !bytes.empty(); ++flip) {
                    bytes[random.Below(bytes.size())] ^= static_cast<std::uint8_t>(1U << random.Below(8U));
                }
            }
            rig.server->HandleMessage(1U, channel, bytes, now + iteration);
        }
        Require(rig.server->ReplicatedCount() == replicated, "hostile messages changed the set of replicated entities");
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            const NetworkId id = static_cast<NetworkId>(index + 1U);
            if (rig.server->OwnerOf(id) == 1U) {
                continue;
            }
            const Position* position = rig.world->TryGet<Position>(entities[index]);
            const Health* health = rig.world->TryGet<Health>(entities[index]);
            Require(position->x == before[index].x && position->y == before[index].y && health->hp == healthBefore[index].hp, "hostile messages changed an entity the sender does not own");
            Require(rig.server->OwnerOf(id) == (index % 2 == 0 ? 1U : 2U), "hostile messages changed an owner");
        }
        Require(rig.server->Stats().rejectedMalformed + rig.server->Stats().rpcRejectedMalformed + rig.server->Stats().rpcRejectedArguments > 1000U,
            "the fuzz did not reach the server parsers");
    }
    // The real connections went on working throughout.
    Require(alice.client->State() == ClientState::Connected, "the client was dropped");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "codecs", &TestCodecs },
        { "declarations-round-trip", &TestDeclarationsRoundTrip },
        { "declarations-invalid", &TestDeclarationsRejectBadRange },
        { "unknown-field-load", &TestUnknownFieldIsRefusedAtLoad },
        { "declarations-hello-mismatch", &TestClientWithDifferentDeclarationsIsRefused },
        { "reflected-fields-round-trip", &TestReflectedFieldsRoundTrip },
        { "reflected-fields-clamp", &TestReflectedCodecClampsOutOfRange },
        { "rpc-registry-and-wire", &TestRpcRegistryAndWire },
        { "replication-perfect-network", &TestReplicationOnPerfectNetwork },
#if defined(KB_NET_WEBRTC)
        { "replication-webrtc", &TestReplicationOverWebRtc },
#endif
        { "replication-bad-network", &TestReplicationOnBadNetwork },
        { "replication-many-seeds", &TestReplicationManySeeds },
        { "rpc-and-permissions", &TestRpcAndPermissions },
        { "prediction", &TestPredictionAndReconciliation },
        { "interpolation", &TestSnapshotInterpolation },
        { "registry-mismatch", &TestRegistryMismatchIsRefused },
        { "relevance", &TestRelevance },
        { "owner-only-audience", &TestOwnerOnlyGroupReachesOnlyOwner },
        { "others-audience", &TestOthersGroupSkipsOwner },
        { "ownership-audience-transfer", &TestOwnershipChangeMovesOwnerOnlyData },
        { "initial-only-audience", &TestInitialOnlyGroupIsSentOnce },
        { "audience-fingerprint", &TestRegistryFingerprintCoversAudience },
        { "thirty-two-groups", &TestThirtyTwoGroupsAndRemovalRoundTrip },
        { "scene-write-dirty", &TestSceneWritesMarkReplicationDirty },
        { "dirty-capture-cost", &TestTickCostScalesWithChangedNotTotal },
        { "rotation-smallest-three", &TestRotationSmallestThreeRoundTrip },
        { "position-six-bytes", &TestPositionOnlyChangeCostsSixBytes },
        { "scale-sent-once", &TestScaleSentOnceWhenUnchanged },
        { "changed-filter-new-and-moved", &TestChangedFilterReportsNewAndMovedEntities },
        { "grid-matches-brute-force", &TestGridVisibilityMatchesBruteForce },
        { "visibility-world-size", &TestVisibilityCostIsIndependentOfWorldSize },
        { "global-and-owned-visibility", &TestAlwaysRelevantAndOwnedAreVisibleEverywhere },
        { "million-network-objects", &TestMillionObjectsAreAcceptedAndWarned },
        { "objects-above-4096", &TestNoSilentDropAbove4096 },
        { "slot-reuse-generation", &TestSlotReuseBumpsGeneration },
        { "limits", &TestLimits },
        { "rate-never-exceeds-request", &TestRateNeverExceedsClientRequest },
        { "near-objects-under-budget", &TestNearObjectsKeepFullRateUnderBudget },
        { "rate-loss-and-recovery", &TestRateBacksOffUnderLossAndRecovers },
        { "no-ack-circuit-breaker", &TestCircuitBreakerStopsStateWhenNoAcks },
        { "large-entity-split", &TestLargeEntityIsSplitAcrossEntries },
        { "hostile-messages", &TestHostileReplicationMessages },
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        std::printf("START %s\n", name);
        std::fflush(stdout);
        try {
            run();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& error) {
            std::printf("FAIL %s: %s\n", name, error.what());
            ++failures;
        }
        std::fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}
