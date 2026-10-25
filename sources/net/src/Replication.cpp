#include "kb/net/Replication.hpp"

#include "kb/net/ByteIO.hpp"

#include <exception>
#include <utility>

namespace kb::net {

namespace {

constexpr std::uint8_t kMsgHello = 1U;
constexpr std::uint8_t kMsgSpawn = 2U;
constexpr std::uint8_t kMsgDespawn = 3U;
constexpr std::uint8_t kMsgOwner = 4U;
constexpr std::uint8_t kMsgRpc = 5U;
constexpr std::uint8_t kMsgHelloAck = 6U;
constexpr std::uint8_t kMsgState = 0x10U;
constexpr std::uint8_t kMsgAck = 0x11U;
constexpr std::uint8_t kMsgRpcUnreliable = 0x12U;

constexpr std::uint8_t kReliableChannel = 0U;
constexpr std::uint8_t kUnreliableChannel = 1U;

constexpr std::size_t kStateHeaderBytes = 11U; // type, tick, update id, entry count
constexpr std::size_t kEntryHeaderBytes = 8U;  // network id, changed mask, removed mask
constexpr std::size_t kMaxEntriesPerState = 512U;

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

[[nodiscard]] std::size_t MaskedSize(const ReplicationRegistry& registry, std::uint16_t mask) {
    std::size_t total = 0U;
    for (std::size_t index = 0U; index < registry.Count(); ++index) {
        if ((mask & (1U << index)) != 0U) {
            total += registry.At(index).EncodedSize();
        }
    }
    return total;
}

[[nodiscard]] std::uint16_t AllMask(const ReplicationRegistry& registry) noexcept {
    return static_cast<std::uint16_t>(registry.Count() >= 16U ? 0xFFFFU : (1U << registry.Count()) - 1U);
}

// True if update id `later` is newer than `earlier` in 32 bit wrap-around order.
[[nodiscard]] bool IsNewer(std::uint32_t later, std::uint32_t earlier) noexcept {
    const std::uint32_t difference = later - earlier;
    return difference != 0U && difference < 0x80000000U;
}

} // namespace

// ------------------------------------------------------------ registry

bool ReplicationRegistry::Add(std::unique_ptr<ComponentCodec> codec, std::string& error) {
    if (codec == nullptr || !codec->Valid()) {
        error = "the codec is invalid";
        return false;
    }
    if (codecs_.size() >= kMaxReplicatedComponents) {
        error = "too many replicated components";
        return false;
    }
    for (const auto& existing : codecs_) {
        if (existing->Name() == codec->Name()) {
            error = "the component '" + codec->Name() + "' is already registered";
            return false;
        }
    }
    codecs_.push_back(std::move(codec));
    return true;
}

std::uint64_t ReplicationRegistry::Fingerprint() const noexcept {
    std::uint64_t hash = kFnvOffset;
    const auto mix = [&hash](std::uint8_t byte) {
        hash ^= byte;
        hash *= kFnvPrime;
    };
    mix(static_cast<std::uint8_t>(codecs_.size()));
    for (const auto& codec : codecs_) {
        for (const char character : codec->Describe()) {
            mix(static_cast<std::uint8_t>(character));
        }
        mix(0U);
    }
    return hash;
}

bool RadiusRelevance::IsRelevant(std::uint64_t clientId, NetworkId id, kb::ecs::Entity entity) {
    float viewerX = 0.0F;
    float viewerY = 0.0F;
    float x = 0.0F;
    float y = 0.0F;
    if (!viewer_(clientId, viewerX, viewerY) || !entity_(entity, x, y)) {
        inside_.erase({ clientId, id });
        return false;
    }
    const float dx = x - viewerX;
    const float dy = y - viewerY;
    const float distanceSquared = dx * dx + dy * dy;
    const auto key = std::make_pair(clientId, id);
    const bool wasInside = inside_.contains(key);
    const float limit = wasInside ? radius_ * hysteresis_ : radius_;
    const bool inside = distanceSquared <= limit * limit;
    if (inside) {
        inside_.insert(key);
    } else {
        inside_.erase(key);
    }
    return inside;
}

// -------------------------------------------------------------- server

ReplicationServer::ReplicationServer(Host& host, kb::ecs::World& world, const ReplicationRegistry& components, const RpcRegistry& rpcs,
    ReplicationConfig config)
    : host_(&host), world_(&world), components_(&components), rpcs_(&rpcs), config_(config) {}

NetworkId ReplicationServer::Replicate(kb::ecs::Entity entity, std::uint64_t owner) {
    if (!world_->IsAlive(entity) || entities_.size() >= config_.maxEntities || byEntity_.contains(entity.Id()) || nextId_ == 0U) {
        return 0U;
    }
    const NetworkId id = nextId_++;
    Replicated replicated;
    replicated.entity = entity;
    replicated.owner = owner;
    Capture(replicated);
    entities_.emplace(id, std::move(replicated));
    byEntity_.emplace(entity.Id(), id);
    return id;
}

bool ReplicationServer::Unreplicate(NetworkId id) {
    const auto found = entities_.find(id);
    if (found == entities_.end()) {
        return false;
    }
    byEntity_.erase(found->second.entity.Id());
    entities_.erase(found); // clients that still see it get a Despawn in the next Tick
    return true;
}

bool ReplicationServer::SetOwner(NetworkId id, std::uint64_t owner) {
    const auto found = entities_.find(id);
    if (found == entities_.end()) {
        return false;
    }
    found->second.owner = owner;
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgOwner);
    writer.U32(id);
    writer.U64(owner);
    for (auto& [client, state] : clients_) {
        if (state.ready && state.visible.contains(id)) {
            static_cast<void>(SendReliable(client, message));
        }
    }
    return true;
}

kb::ecs::Entity ReplicationServer::EntityOf(NetworkId id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? kb::ecs::Entity{} : found->second.entity;
}

std::uint64_t ReplicationServer::OwnerOf(NetworkId id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? 0U : found->second.owner;
}

std::size_t ReplicationServer::ReadyClients() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(clients_, [](const auto& entry) { return entry.second.ready; }));
}

std::size_t ReplicationServer::VisibleCount(std::uint64_t client) const {
    const auto found = clients_.find(client);
    return found == clients_.end() ? 0U : found->second.visible.size();
}

void ReplicationServer::Capture(Replicated& replicated) {
    replicated.presentMask = 0U;
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        const ComponentCodec& codec = components_->At(index);
        std::vector<std::uint8_t> bytes(codec.EncodedSize());
        if (codec.Capture(*world_, replicated.entity, bytes)) {
            replicated.current[index] = std::move(bytes);
            replicated.presentMask = static_cast<std::uint16_t>(replicated.presentMask | (1U << index));
        } else {
            replicated.current[index].clear();
        }
    }
}

bool ReplicationServer::SendReliable(std::uint64_t client, std::span<const std::uint8_t> message) {
    if (host_->Send(client, kReliableChannel, message) == SendResult::Queued) {
        return true;
    }
    ++stats_.sendBusy;
    return false;
}

bool ReplicationServer::SendSpawn(std::uint64_t client, ClientState& state, NetworkId id, const Replicated& replicated, std::uint32_t tick) {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgSpawn);
    writer.U32(id);
    writer.U64(replicated.owner);
    writer.U32(tick);
    writer.U16(replicated.presentMask);
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        if ((replicated.presentMask & (1U << index)) != 0U) {
            writer.Bytes(replicated.current[index]);
        }
    }
    if (!SendReliable(client, message)) {
        return false;
    }
    state.visible.insert(id);
    // The client has only what the Spawn carried. A new epoch makes acknowledgements of updates sent before an
    // earlier Despawn of this entity (when it left and came back into view) worthless.
    Baseline fresh;
    fresh.epoch = state.nextEpoch++;
    state.baseline[id] = std::move(fresh);
    ++stats_.spawnsSent;
    return true;
}

void ReplicationServer::Despawn(std::uint64_t client, ClientState& state, NetworkId id) {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgDespawn);
    writer.U32(id);
    if (SendReliable(client, message)) {
        state.visible.erase(id);
        state.baseline.erase(id);
        ++stats_.despawnsSent;
    }
}

void ReplicationServer::UpdateVisibility(std::uint64_t client, ClientState& state, std::uint32_t tick) {
    const std::vector<NetworkId> seen(state.visible.begin(), state.visible.end());
    for (const NetworkId id : seen) {
        const auto found = entities_.find(id);
        const bool relevant = found != entities_.end() && (relevance_ == nullptr || relevance_->IsRelevant(client, id, found->second.entity));
        if (!relevant) {
            Despawn(client, state, id);
        }
    }
    for (const auto& [id, replicated] : entities_) {
        if (state.visible.contains(id)) {
            continue;
        }
        if (relevance_ == nullptr || relevance_->IsRelevant(client, id, replicated.entity)) {
            if (!SendSpawn(client, state, id, replicated, tick)) {
                break; // the send queue is full: try the rest next tick
            }
        }
    }
}

void ReplicationServer::BuildUpdates(std::uint64_t client, ClientState& state, std::uint32_t tick) {
    if (state.visible.empty()) {
        return;
    }
    std::vector<NetworkId> order;
    order.reserve(state.visible.size());
    for (auto it = state.visible.lower_bound(state.cursor); it != state.visible.end(); ++it) {
        order.push_back(*it);
    }
    for (auto it = state.visible.begin(); it != state.visible.end() && *it < state.cursor; ++it) {
        order.push_back(*it);
    }

    std::vector<std::uint8_t> body;
    std::vector<SentEntry> entries;
    std::uint16_t count = 0U;
    const auto flush = [&]() -> bool {
        if (count == 0U) {
            return true;
        }
        std::vector<std::uint8_t> message;
        ByteWriter writer(message);
        writer.U8(kMsgState);
        writer.U32(tick);
        const std::uint32_t updateId = state.nextUpdateId;
        writer.U32(updateId);
        writer.U16(count);
        writer.Bytes(body);
        if (host_->Send(client, kUnreliableChannel, message) != SendResult::Queued) {
            ++stats_.sendBusy;
            return false;
        }
        state.nextUpdateId = updateId + 1U == 0U ? 1U : updateId + 1U;
        state.inFlight[updateId] = std::move(entries);
        while (state.inFlight.size() > config_.maxUpdatesInFlight) {
            state.inFlight.erase(state.inFlight.begin());
        }
        ++stats_.stateMessagesSent;
        body.clear();
        entries.clear();
        count = 0U;
        return true;
    };

    std::size_t budget = config_.maxBytesPerClientPerTick;
    bool finished = true;
    for (const NetworkId id : order) {
        const auto replicatedFound = entities_.find(id);
        if (replicatedFound == entities_.end()) {
            continue;
        }
        const Replicated& replicated = replicatedFound->second;
        Baseline& baseline = state.baseline[id];
        std::uint16_t changed = 0U;
        std::uint16_t removed = 0U;
        for (std::size_t index = 0U; index < components_->Count(); ++index) {
            const std::uint16_t bit = static_cast<std::uint16_t>(1U << index);
            const bool present = (replicated.presentMask & bit) != 0U;
            const bool had = (baseline.presentMask & bit) != 0U;
            if (present) {
                if (!had || baseline.bytes[index] != replicated.current[index]) {
                    changed = static_cast<std::uint16_t>(changed | bit);
                }
            } else if (had) {
                removed = static_cast<std::uint16_t>(removed | bit);
            }
        }
        if (changed == 0U && removed == 0U) {
            continue;
        }
        const std::size_t entrySize = kEntryHeaderBytes + MaskedSize(*components_, changed);
        if (kStateHeaderBytes + entrySize > config_.maxUpdateBytes) {
            continue; // cannot fit in a message at all; the configuration is too small for this entity
        }
        if (kStateHeaderBytes + body.size() + entrySize > config_.maxUpdateBytes || count == kMaxEntriesPerState) {
            if (!flush()) {
                state.cursor = id;
                finished = false;
                break;
            }
        }
        if (entrySize > budget && budget != config_.maxBytesPerClientPerTick) {
            state.cursor = id;
            finished = false;
            break;
        }
        budget = entrySize >= budget ? 0U : budget - entrySize;
        ByteWriter writer(body);
        writer.U32(id);
        writer.U16(changed);
        writer.U16(removed);
        SentEntry sent;
        sent.id = id;
        sent.epoch = baseline.epoch;
        sent.tick = tick;
        sent.changedMask = changed;
        sent.removedMask = removed;
        for (std::size_t index = 0U; index < components_->Count(); ++index) {
            if ((changed & (1U << index)) != 0U) {
                writer.Bytes(replicated.current[index]);
                sent.bytes[index] = replicated.current[index];
            }
        }
        entries.push_back(std::move(sent));
        ++count;
        ++stats_.entriesSent;
    }
    if (!flush()) {
        finished = false;
    }
    if (finished) {
        state.cursor = 0U;
    }
}

void ReplicationServer::Tick(std::uint32_t tick, std::uint64_t) {
    lastTick_ = tick;
    for (auto it = entities_.begin(); it != entities_.end();) {
        if (!world_->IsAlive(it->second.entity)) {
            byEntity_.erase(it->second.entity.Id());
            it = entities_.erase(it);
            continue; // clients are sent a Despawn below
        }
        Capture(it->second);
        ++it;
    }
    for (auto& [client, state] : clients_) {
        if (!state.ready) {
            continue;
        }
        UpdateVisibility(client, state, tick);
        BuildUpdates(client, state, tick);
    }
}

void ReplicationServer::HandleEvent(const NetEvent& event, std::uint64_t nowMilliseconds) {
    switch (event.type) {
    case NetEventType::Connected: {
        clients_[event.peer] = ClientState{};
        std::vector<std::uint8_t> hello;
        ByteWriter writer(hello);
        writer.U8(kMsgHello);
        writer.U64(components_->Fingerprint());
        writer.U64(rpcs_->Fingerprint());
        writer.U32(lastTick_);
        static_cast<void>(SendReliable(event.peer, hello));
        break;
    }
    case NetEventType::Disconnected:
        clients_.erase(event.peer);
        limiter_.Forget(event.peer);
        break;
    case NetEventType::Message:
        HandleMessage(event.peer, event.message.channel, event.message.data, nowMilliseconds);
        break;
    }
}

void ReplicationServer::HandleMessage(std::uint64_t client, std::uint8_t channel, std::span<const std::uint8_t> data, std::uint64_t nowMilliseconds) {
    const auto found = clients_.find(client);
    if (found == clients_.end()) {
        return;
    }
    ClientState& state = found->second;
    if (data.empty()) {
        ++stats_.rejectedMalformed;
        return;
    }
    switch (data[0]) {
    case kMsgHelloAck: {
        ByteReader reader(data.subspan(1));
        const std::uint64_t replication = reader.U64();
        const std::uint64_t rpc = reader.U64();
        if (channel != kReliableChannel || !reader.Ok() || reader.Remaining() != 0U) {
            ++stats_.rejectedMalformed;
        } else if (replication != components_->Fingerprint() || rpc != rpcs_->Fingerprint()) {
            ++stats_.helloMismatch;
            host_->Disconnect(client);
        } else {
            state.ready = true;
        }
        break;
    }
    case kMsgAck:
        if (channel != kUnreliableChannel) {
            ++stats_.rejectedMalformed;
        } else if (!state.ready) {
            ++stats_.rejectedNotReady;
        } else {
            HandleAck(state, data);
        }
        break;
    case kMsgRpc:
    case kMsgRpcUnreliable: {
        const bool reliable = data[0] == kMsgRpc;
        if (channel != (reliable ? kReliableChannel : kUnreliableChannel)) {
            ++stats_.rpcRejectedMalformed;
        } else if (!state.ready) {
            ++stats_.rejectedNotReady;
        } else {
            HandleRpc(client, channel, reliable, data, nowMilliseconds);
        }
        break;
    }
    default:
        ++stats_.rejectedMalformed;
        break;
    }
}

void ReplicationServer::HandleAck(ClientState& state, std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const std::uint32_t latest = reader.U32();
    const std::uint32_t bits = reader.U32();
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    ++stats_.acksReceived;
    const auto acknowledge = [&](std::uint32_t updateId) {
        const auto sent = state.inFlight.find(updateId);
        if (sent != state.inFlight.end()) {
            ApplyAcked(state, sent->second);
            state.inFlight.erase(sent);
        }
    };
    acknowledge(latest);
    for (std::uint32_t index = 0U; index < 32U; ++index) {
        if (((bits >> index) & 1U) != 0U) {
            acknowledge(latest - 1U - index);
        }
    }
}

void ReplicationServer::ApplyAcked(ClientState& state, const std::vector<SentEntry>& entries) {
    for (const SentEntry& entry : entries) {
        const auto baseline = state.baseline.find(entry.id);
        if (baseline == state.baseline.end() || entry.epoch != baseline->second.epoch || entry.tick <= baseline->second.tick) {
            continue; // despawned (and maybe spawned again) meanwhile, or a newer update was acknowledged first
        }
        Baseline& target = baseline->second;
        for (std::size_t index = 0U; index < components_->Count(); ++index) {
            const std::uint16_t bit = static_cast<std::uint16_t>(1U << index);
            if ((entry.changedMask & bit) != 0U) {
                target.bytes[index] = entry.bytes[index];
                target.presentMask = static_cast<std::uint16_t>(target.presentMask | bit);
            }
            if ((entry.removedMask & bit) != 0U) {
                target.bytes[index].clear();
                target.presentMask = static_cast<std::uint16_t>(target.presentMask & ~bit);
            }
        }
        target.tick = entry.tick;
    }
}

void ReplicationServer::HandleRpc(std::uint64_t client, std::uint8_t, bool reliable, std::span<const std::uint8_t> data, std::uint64_t nowMilliseconds) {
    ++stats_.rpcReceived;
    std::uint16_t id = 0U;
    std::vector<RpcValue> args;
    switch (DecodeRpc(*rpcs_, data.subspan(1), RpcDirection::ClientToServer, id, args)) {
    case RpcStatus::Ok:
        break;
    case RpcStatus::WrongDirection:
        ++stats_.rpcRejectedDirection;
        return;
    case RpcStatus::BadArguments:
        ++stats_.rpcRejectedArguments;
        return;
    default:
        ++stats_.rpcRejectedMalformed;
        return;
    }
    const RpcDefinition* definition = rpcs_->Find(id);
    if (definition == nullptr || definition->reliable != reliable) {
        ++stats_.rpcRejectedMalformed;
        return;
    }
    if (!limiter_.Allow(client, id, definition->maxCallsPerSecond, nowMilliseconds)) {
        ++stats_.rpcRejectedRate;
        return;
    }
    if (definition->ownerArg >= 0) {
        const NetworkId target = std::get<std::uint32_t>(args[static_cast<std::size_t>(definition->ownerArg)]);
        const auto replicated = entities_.find(target);
        if (replicated == entities_.end() || replicated->second.owner != client) {
            ++stats_.rpcRejectedPermission;
            return;
        }
    }
    const auto handler = handlers_.find(id);
    if (handler == handlers_.end()) {
        ++stats_.rpcNoHandler;
        return;
    }
    try {
        handler->second(client, args);
        ++stats_.rpcDispatched;
    } catch (const std::exception&) {
        ++stats_.rpcHandlerErrors;
    } catch (...) {
        ++stats_.rpcHandlerErrors;
    }
}

void ReplicationServer::SetRpcHandler(const std::string& name, RpcHandler handler) {
    std::uint16_t id = 0U;
    if (rpcs_->FindByName(name, id) != nullptr) {
        handlers_[id] = std::move(handler);
    }
}

RpcStatus ReplicationServer::SendRpc(std::uint64_t client, std::uint16_t id, const RpcDefinition& definition, std::span<const RpcValue> args) {
    std::vector<std::uint8_t> message;
    message.push_back(definition.reliable ? kMsgRpc : kMsgRpcUnreliable);
    if (const RpcStatus status = EncodeRpc(*rpcs_, id, args, message); status != RpcStatus::Ok) {
        return status;
    }
    return host_->Send(client, definition.reliable ? kReliableChannel : kUnreliableChannel, message) == SendResult::Queued ? RpcStatus::Ok : RpcStatus::Busy;
}

RpcStatus ReplicationServer::CallRpc(std::uint64_t client, const std::string& name, std::span<const RpcValue> args) {
    std::uint16_t id = 0U;
    const RpcDefinition* definition = rpcs_->FindByName(name, id);
    if (definition == nullptr) {
        return RpcStatus::UnknownRpc;
    }
    if (definition->direction != RpcDirection::ServerToClient) {
        return RpcStatus::WrongDirection;
    }
    const auto found = clients_.find(client);
    if (found == clients_.end() || !found->second.ready) {
        return RpcStatus::NotReady;
    }
    return SendRpc(client, id, *definition, args);
}

void ReplicationServer::CallRpcAll(const std::string& name, std::span<const RpcValue> args, RpcStatus& status) {
    status = RpcStatus::Ok;
    for (const auto& [client, state] : clients_) {
        if (!state.ready) {
            continue;
        }
        const RpcStatus result = CallRpc(client, name, args);
        if (result != RpcStatus::Ok && status == RpcStatus::Ok) {
            status = result;
        }
    }
}

// -------------------------------------------------------------- client

ReplicationClient::ReplicationClient(Client& client, kb::ecs::World& world, const ReplicationRegistry& components, const RpcRegistry& rpcs,
    ReplicationConfig config)
    : client_(&client), world_(&world), components_(&components), rpcs_(&rpcs), config_(config) {}

void ReplicationClient::Reset() {
    for (const auto& [id, replica] : replicas_) {
        if (world_->IsAlive(replica.entity)) {
            world_->DestroyEntity(replica.entity);
        }
    }
    replicas_.clear();
    ready_ = false;
    latestUpdate_ = 0U;
    receivedBits_ = 0U;
    ackDirty_ = false;
    helloAckPending_ = false;
}

kb::ecs::Entity ReplicationClient::Find(NetworkId id) const {
    const auto found = replicas_.find(id);
    return found == replicas_.end() ? kb::ecs::Entity{} : found->second.entity;
}

std::uint64_t ReplicationClient::OwnerOf(NetworkId id) const {
    const auto found = replicas_.find(id);
    return found == replicas_.end() ? 0U : found->second.owner;
}

std::vector<NetworkId> ReplicationClient::ReplicaIds() const {
    std::vector<NetworkId> ids;
    ids.reserve(replicas_.size());
    for (const auto& entry : replicas_) {
        ids.push_back(entry.first);
    }
    return ids;
}

void ReplicationClient::HandleEvent(const NetEvent& event) {
    switch (event.type) {
    case NetEventType::Connected:
    case NetEventType::Disconnected:
        Reset();
        break;
    case NetEventType::Message:
        HandleMessage(event.message.channel, event.message.data);
        break;
    }
}

void ReplicationClient::HandleMessage(std::uint8_t channel, std::span<const std::uint8_t> data) {
    if (data.empty()) {
        ++stats_.rejectedMalformed;
        return;
    }
    const std::uint8_t type = data[0];
    const bool reliableType = type == kMsgHello || type == kMsgSpawn || type == kMsgDespawn || type == kMsgOwner || type == kMsgRpc;
    const bool unreliableType = type == kMsgState || type == kMsgRpcUnreliable;
    if ((reliableType && channel != kReliableChannel) || (unreliableType && channel != kUnreliableChannel) || (!reliableType && !unreliableType)) {
        ++stats_.rejectedMalformed;
        return;
    }
    if (type == kMsgHello) {
        HandleHello(data);
        return;
    }
    if (!ready_) {
        ++stats_.rejectedNotReady;
        return;
    }
    switch (type) {
    case kMsgSpawn: HandleSpawn(data); break;
    case kMsgDespawn: HandleDespawn(data); break;
    case kMsgOwner: HandleOwner(data); break;
    case kMsgState: HandleState(data); break;
    default: HandleRpc(data); break;
    }
}

void ReplicationClient::HandleHello(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const std::uint64_t replication = reader.U64();
    const std::uint64_t rpc = reader.U64();
    static_cast<void>(reader.U32());
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    if (replication != components_->Fingerprint() || rpc != rpcs_->Fingerprint()) {
        ++stats_.helloMismatch;
        client_->Disconnect();
        return;
    }
    ready_ = true;
    helloAckPending_ = true; // sent by Flush, and repeated there until the session takes it
    Flush();
}

void ReplicationClient::HandleSpawn(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const NetworkId id = reader.U32();
    const std::uint64_t owner = reader.U64();
    const std::uint32_t tick = reader.U32();
    const std::uint16_t mask = reader.U16();
    if (!reader.Ok() || id == 0U || (mask & ~AllMask(*components_)) != 0U || reader.Remaining() != MaskedSize(*components_, mask)) {
        ++stats_.rejectedMalformed;
        return;
    }
    std::array<std::span<const std::uint8_t>, kMaxReplicatedComponents> parts;
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        if ((mask & (1U << index)) != 0U) {
            parts[index] = reader.Bytes(components_->At(index).EncodedSize());
            if (!components_->At(index).Validate(parts[index])) {
                ++stats_.rejectedMalformed;
                return;
            }
        }
    }
    if (replicas_.contains(id)) {
        return; // a repeated Spawn: the entity is already here
    }
    if (replicas_.size() >= config_.maxEntities) {
        ++stats_.rejectedLimit;
        return;
    }
    const kb::ecs::Entity entity = world_->CreateEntity();
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        if ((mask & (1U << index)) != 0U) {
            components_->At(index).Apply(*world_, entity, parts[index]);
        }
    }
    replicas_.emplace(id, Replica{ entity, owner, tick });
    ++stats_.spawnsApplied;
}

void ReplicationClient::HandleDespawn(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const NetworkId id = reader.U32();
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    const auto found = replicas_.find(id);
    if (found == replicas_.end()) {
        return;
    }
    if (world_->IsAlive(found->second.entity)) {
        world_->DestroyEntity(found->second.entity);
    }
    replicas_.erase(found);
    ++stats_.despawnsApplied;
}

void ReplicationClient::HandleOwner(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const NetworkId id = reader.U32();
    const std::uint64_t owner = reader.U64();
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    const auto found = replicas_.find(id);
    if (found != replicas_.end()) {
        found->second.owner = owner;
        ++stats_.ownerChanges;
    }
}

void ReplicationClient::HandleState(std::span<const std::uint8_t> data) {
    struct Entry {
        NetworkId id = 0U;
        std::uint16_t changed = 0U;
        std::uint16_t removed = 0U;
        std::array<std::span<const std::uint8_t>, kMaxReplicatedComponents> parts;
    };
    ByteReader reader(data.subspan(1));
    const std::uint32_t tick = reader.U32();
    const std::uint32_t updateId = reader.U32();
    const std::uint16_t count = reader.U16();
    if (!reader.Ok() || updateId == 0U || count > kMaxEntriesPerState) {
        ++stats_.rejectedMalformed;
        return;
    }
    const std::uint16_t valid = AllMask(*components_);
    std::vector<Entry> entries;
    entries.reserve(count);
    for (std::uint16_t index = 0U; index < count; ++index) {
        Entry entry;
        entry.id = reader.U32();
        entry.changed = reader.U16();
        entry.removed = reader.U16();
        if (!reader.Ok() || entry.id == 0U || ((entry.changed | entry.removed) & ~valid) != 0U || (entry.changed & entry.removed) != 0U ||
            (entry.changed | entry.removed) == 0U) {
            ++stats_.rejectedMalformed;
            return;
        }
        for (std::size_t component = 0U; component < components_->Count(); ++component) {
            if ((entry.changed & (1U << component)) != 0U) {
                entry.parts[component] = reader.Bytes(components_->At(component).EncodedSize());
                if (!reader.Ok() || !components_->At(component).Validate(entry.parts[component])) {
                    ++stats_.rejectedMalformed;
                    return;
                }
            }
        }
        entries.push_back(entry);
    }
    if (reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }

    // The message is well formed: apply it. The server treats an acknowledged message as delivered state, so
    // a message with an entry for an entity this client does not have (yet), or older than what the entity
    // already shows, is not acknowledged; the next update repeats everything that is still different.
    bool acknowledge = true;
    for (const Entry& entry : entries) {
        const auto found = replicas_.find(entry.id);
        if (found == replicas_.end()) {
            ++stats_.entriesUnknownEntity;
            acknowledge = false;
            continue;
        }
        if (tick <= found->second.lastTick) {
            ++stats_.entriesStale;
            acknowledge = acknowledge && tick == found->second.lastTick;
            continue;
        }
        for (std::size_t component = 0U; component < components_->Count(); ++component) {
            if ((entry.changed & (1U << component)) != 0U) {
                components_->At(component).Apply(*world_, found->second.entity, entry.parts[component]);
            }
            if ((entry.removed & (1U << component)) != 0U) {
                components_->At(component).Remove(*world_, found->second.entity);
            }
        }
        found->second.lastTick = tick;
        ++stats_.entriesApplied;
    }
    ++stats_.stateMessagesApplied;
    if (!acknowledge) {
        return;
    }

    if (latestUpdate_ == 0U) {
        latestUpdate_ = updateId;
        receivedBits_ = 0U;
    } else if (IsNewer(updateId, latestUpdate_)) {
        const std::uint32_t shift = updateId - latestUpdate_;
        receivedBits_ = shift > 32U ? 0U : (shift == 32U ? 0U : receivedBits_ << shift) | (1U << (shift - 1U));
        latestUpdate_ = updateId;
    } else if (updateId != latestUpdate_) {
        const std::uint32_t distance = latestUpdate_ - 1U - updateId;
        if (distance < 32U) {
            receivedBits_ |= 1U << distance;
        }
    }
    ackDirty_ = true;
}

void ReplicationClient::Flush() {
    if (!ready_) {
        return;
    }
    if (helloAckPending_) {
        std::vector<std::uint8_t> hello;
        ByteWriter helloWriter(hello);
        helloWriter.U8(kMsgHelloAck);
        helloWriter.U64(components_->Fingerprint());
        helloWriter.U64(rpcs_->Fingerprint());
        if (client_->Send(kReliableChannel, hello) != SendResult::Queued) {
            return;
        }
        helloAckPending_ = false;
    }
    if (!ackDirty_) {
        return;
    }
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgAck);
    writer.U32(latestUpdate_);
    writer.U32(receivedBits_);
    if (client_->Send(kUnreliableChannel, message) == SendResult::Queued) {
        ackDirty_ = false;
        ++stats_.acksSent;
    }
}

void ReplicationClient::HandleRpc(std::span<const std::uint8_t> data) {
    ++stats_.rpcReceived;
    std::uint16_t id = 0U;
    std::vector<RpcValue> args;
    const bool reliable = data[0] == kMsgRpc;
    if (DecodeRpc(*rpcs_, data.subspan(1), RpcDirection::ServerToClient, id, args) != RpcStatus::Ok) {
        ++stats_.rpcRejected;
        return;
    }
    const RpcDefinition* definition = rpcs_->Find(id);
    if (definition == nullptr || definition->reliable != reliable) {
        ++stats_.rpcRejected;
        return;
    }
    const auto handler = handlers_.find(id);
    if (handler == handlers_.end()) {
        ++stats_.rpcNoHandler;
        return;
    }
    try {
        handler->second(args);
        ++stats_.rpcDispatched;
    } catch (...) {
        ++stats_.rpcHandlerErrors;
    }
}

void ReplicationClient::SetRpcHandler(const std::string& name, RpcHandler handler) {
    std::uint16_t id = 0U;
    if (rpcs_->FindByName(name, id) != nullptr) {
        handlers_[id] = std::move(handler);
    }
}

RpcStatus ReplicationClient::CallRpc(const std::string& name, std::span<const RpcValue> args) {
    std::uint16_t id = 0U;
    const RpcDefinition* definition = rpcs_->FindByName(name, id);
    if (definition == nullptr) {
        return RpcStatus::UnknownRpc;
    }
    if (definition->direction != RpcDirection::ClientToServer) {
        return RpcStatus::WrongDirection;
    }
    if (!ready_) {
        return RpcStatus::NotReady;
    }
    std::vector<std::uint8_t> message;
    message.push_back(definition->reliable ? kMsgRpc : kMsgRpcUnreliable);
    if (const RpcStatus status = EncodeRpc(*rpcs_, id, args, message); status != RpcStatus::Ok) {
        return status;
    }
    return client_->Send(definition->reliable ? kReliableChannel : kUnreliableChannel, message) == SendResult::Queued ? RpcStatus::Ok : RpcStatus::Busy;
}

} // namespace kb::net
