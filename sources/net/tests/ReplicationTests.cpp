// Tests for the replication and RPC layer of kb_net: codecs, RPC validation, state convergence on a
// perfect and on a bad network, permissions, relevance, hostile messages.

#include "kb/net/Prediction.hpp"
#include "kb/net/Replication.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
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

    explicit Rig(LinkConditions conditions = {}, ReplicationConfig config = {}) : network(conditions), config_(config) {
        components = MakeComponents();
        rpcs = MakeRpcs();
        hostPort = network.CreateTransport(Endpoint::Loopback(9000));
        keys.Set(1U, MakeKey(1U));
        keys.Set(2U, MakeKey(2U));
        keys.Set(3U, MakeKey(3U));
        HostConfig hostConfig;
        host = std::make_unique<Host>(*hostPort, hostConfig, keys);
        world = std::make_unique<kb::ecs::World>();
        server = std::make_unique<ReplicationServer>(*host, *world, *components, *rpcs, config);
    }

    Peer& AddPeer(std::uint64_t id, std::uint16_t port, std::uint64_t now) {
        auto peer = std::make_unique<Peer>();
        peer->id = id;
        peer->port = network.CreateTransport(Endpoint::Loopback(port));
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
                peer->replication->Flush();
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
        for (NetworkId id = 1U; id < 1000U; ++id) {
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
                { why = "check 4 (peer " + std::to_string(peer.id) + ")"; return false; }
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
        world->Set(entity, Position{ x, y, 0.0F });
        world->Set(entity, Health{});
        world->Set(entity, Facing{ 90.0F });
        world->Set(entity, Secret{ 7 });
        const NetworkId networkId = server->Replicate(entity, owner);
        Require(networkId != 0U, "an entity could not be replicated");
        if (id != nullptr) {
            *id = networkId;
        }
        return entity;
    }

    struct Captured {
        std::uint64_t peer;
        std::uint8_t channel;
        std::vector<std::uint8_t> data;
    };

    LoopbackNetwork network;
    ReplicationConfig config_;
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

    const std::vector<RpcValue> good{ std::uint32_t{ 7U }, 12.5F, -3.0F };
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
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ std::uint32_t{ 0U }, 1.0F, 1.0F }, sink) == RpcStatus::BadArguments, "network id 0 was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ std::uint32_t{ 1U }, 1000.0F, 1.0F }, sink) == RpcStatus::BadArguments, "a float out of range was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ std::uint32_t{ 1U }, std::nanf(""), 1.0F }, sink) == RpcStatus::BadArguments, "NaN was accepted");
    Require(EncodeRpc(*rpcs, id, std::vector<RpcValue>{ std::uint32_t{ 1U }, 1.0F }, sink) == RpcStatus::BadArguments, "a missing argument was accepted");
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
    nan[6U] = 0x00U; nan[7U] = 0x00U; nan[8U] = 0xC0U; nan[9U] = 0x7FU; // the first float becomes a NaN
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

void TestReplicationOnPerfectNetwork() {
    Rig rig;
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
        auto port = rig.network.CreateTransport(Endpoint::Loopback(9104U));
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
        const kb::ecs::Entity target = rig.server->EntityOf(std::get<std::uint32_t>(args[0]));
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
    Require(bob.replication->CallRpc("SetTarget", std::vector<RpcValue>{ std::uint32_t{ 999U }, 1.0F, 1.0F }) == RpcStatus::Ok, "bob's call on a missing entity could not be sent");
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
    const NetworkId id = rig.server->Replicate(entity, 1U);
    rig.server->SetRpcHandler("Input", [&](std::uint64_t, std::span<const RpcValue> args) {
        const kb::ecs::Entity target = rig.server->EntityOf(std::get<std::uint32_t>(args[0]));
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
    Rig rig;
    rig.AddPeer(1U, 9101U, 0U);
    std::vector<kb::ecs::Entity> entities;
    std::vector<NetworkId> ids;
    for (int index = 0; index < 3; ++index) {
        NetworkId id = 0U;
        entities.push_back(rig.Spawn(static_cast<float>(index) * 200.0F, 0.0F, 0U, &id));
        ids.push_back(id);
    }
    // The viewer stands at x = 0 and sees 100 units; the entities are at 0, 200 and 400.
    RadiusRelevance relevance(
        100.0F, 1.2F, [](std::uint64_t, float& x, float& y) { x = 0.0F; y = 0.0F; return true; },
        [&rig](kb::ecs::Entity entity, float& x, float& y) {
            const Position* position = rig.world->TryGet<Position>(entity);
            if (position == nullptr) {
                return false;
            }
            x = position->x;
            y = position->y;
            return true;
        });
    rig.server->SetRelevance(&relevance);
    const auto visibleNow = [&](NetworkId id) { return id == ids[0]; };
    std::uint64_t now = rig.Run(0U, 10'000U, [&] { return rig.AllReady() && rig.Converged(*rig.peers[0], visibleNow); });
    Require(rig.Converged(*rig.peers[0], visibleNow) && rig.peers[0]->replication->ReplicaCount() == 1U, "only the near entity should be replicated");

    // Entity 2 walks in (to x = 90) and then to the edge (x = 110, inside the hysteresis band): it stays.
    rig.world->Set(entities[1], Position{ 90.0F, 0.0F, 0.0F });
    bool nearVisible = true;
    const auto visibleTwo = [&](NetworkId id) { return id == ids[0] || id == ids[1]; };
    now = rig.Run(now, 10'000U, [&] { return rig.Converged(*rig.peers[0], visibleTwo); });
    Require(rig.Converged(*rig.peers[0], visibleTwo), "an entity that came into range was not replicated");
    const std::uint64_t spawnsBefore = rig.peers[0]->replication->Stats().spawnsApplied;
    for (int step = 0; step < 20; ++step) {
        rig.world->Set(entities[1], Position{ step % 2 == 0 ? 110.0F : 105.0F, 0.0F, 0.0F });
        now = rig.Run(now, 60U, [] { return false; });
        nearVisible = nearVisible && rig.peers[0]->replication->Find(ids[1]).IsValid();
    }
    Require(nearVisible && rig.peers[0]->replication->Stats().despawnsApplied == 0U && rig.peers[0]->replication->Stats().spawnsApplied == spawnsBefore,
        "an entity at the edge flickered");
    // Out of the band it goes.
    rig.world->Set(entities[1], Position{ 150.0F, 0.0F, 0.0F });
    now = rig.Run(now, 10'000U, [&] { return rig.Converged(*rig.peers[0], visibleNow); });
    Require(rig.Converged(*rig.peers[0], visibleNow) && rig.peers[0]->replication->Stats().despawnsApplied == 1U, "an entity out of range was not despawned");
    Require(rig.server->ReplicatedCount() == 3U, "relevance removed an entity from the server");
}

void TestLimits() {
    ReplicationConfig config;
    config.maxEntities = 4U;
    Rig rig({}, config);
    rig.AddPeer(1U, 9101U, 0U);
    for (int index = 0; index < 4; ++index) {
        static_cast<void>(rig.Spawn(static_cast<float>(index), 0.0F, 0U));
    }
    const kb::ecs::Entity extra = rig.world->CreateEntity();
    Require(rig.server->Replicate(extra) == 0U, "the entity limit was not enforced");
    const kb::ecs::Entity first = rig.server->EntityOf(1U);
    Require(rig.server->Replicate(first) == 0U, "an entity was replicated twice");
    const kb::ecs::Entity dead = rig.world->CreateEntity();
    rig.world->DestroyEntity(dead);
    Require(rig.server->Replicate(dead) == 0U, "a dead entity was replicated");
    rig.Run(0U, 10'000U, [&] { return rig.AllReady() && rig.AllConverged(); });
    Require(rig.AllConverged(), "the limited set did not replicate");

    // A bandwidth limit per tick spreads a big change over several ticks and still converges.
    ReplicationConfig slow;
    slow.maxBytesPerClientPerTick = 120U;
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

void TestHostileReplicationMessages() {
    Rig rig(BadLink());
    Rig::Peer& alice = rig.AddPeer(1U, 9101U, 0U);
    rig.AddPeer(2U, 9102U, 0U);
    std::vector<kb::ecs::Entity> entities;
    for (int index = 0; index < 6; ++index) {
        entities.push_back(rig.Spawn(static_cast<float>(index), 0.0F, index % 2 == 0 ? 1U : 2U));
    }
    rig.server->SetRpcHandler("Heal", [&](std::uint64_t, std::span<const RpcValue> args) {
        const kb::ecs::Entity target = rig.server->EntityOf(std::get<std::uint32_t>(args[0]));
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
        ReplicationClient fuzzed(sinkClient, fuzzWorld, *rig.components, *rig.rpcs, ReplicationConfig{ .maxEntities = 64U });
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
            Require(fuzzed.ReplicaCount() <= 64U, "the replica limit was exceeded");
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
        { "rpc-registry-and-wire", &TestRpcRegistryAndWire },
        { "replication-perfect-network", &TestReplicationOnPerfectNetwork },
        { "replication-bad-network", &TestReplicationOnBadNetwork },
        { "replication-many-seeds", &TestReplicationManySeeds },
        { "rpc-and-permissions", &TestRpcAndPermissions },
        { "prediction", &TestPredictionAndReconciliation },
        { "interpolation", &TestSnapshotInterpolation },
        { "registry-mismatch", &TestRegistryMismatchIsRefused },
        { "relevance", &TestRelevance },
        { "limits", &TestLimits },
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
