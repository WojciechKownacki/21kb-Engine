#include "kb/net/Replication.hpp"

#include "kb/net/ByteIO.hpp"
#include "ReplicationSlots.hpp"
#include "SpatialGrid.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdio>
#include <exception>
#include <utility>
#include <chrono>
#include <cstring>
#include <bit>

namespace kb::net {

namespace {

constexpr std::uint8_t kMsgHello = 1U;
using ProfileClock = std::chrono::steady_clock;
struct ProfileScope {
    bool enabled;
    std::uint64_t& total;
    ProfileClock::time_point start;
    ProfileScope(bool active, std::uint64_t& counter) : enabled(active), total(counter), start(active ? ProfileClock::now() : ProfileClock::time_point{}) {}
    ~ProfileScope() { if (enabled) total += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now() - start).count()); }
};
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
constexpr std::size_t kMaxEntriesPerState = 512U;

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

[[nodiscard]] std::size_t MaskedSize(const ReplicationRegistry& registry, std::uint32_t mask) {
    std::size_t total = 0U;
    for (std::size_t index = 0U; index < registry.Count(); ++index) {
        if ((mask & (1U << index)) != 0U) {
            total += registry.At(index).EncodedSize();
        }
    }
    return total;
}

[[nodiscard]] std::size_t MaskedSize(const ReplicationSlots& slots, std::uint32_t mask) {
    std::size_t size = 0U;
    while (mask != 0U) {
        size += slots.EncodedSize(static_cast<std::size_t>(std::countr_zero(mask)));
        mask &= mask - 1U;
    }
    return size;
}

[[nodiscard]] bool EncodedEqual(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    if (a.size() == 6U) {
        std::uint32_t firstA, firstB;
        std::uint16_t lastA, lastB;
        std::memcpy(&firstA, a.data(), 4U); std::memcpy(&firstB, b.data(), 4U);
        std::memcpy(&lastA, a.data() + 4U, 2U); std::memcpy(&lastB, b.data() + 4U, 2U);
        return firstA == firstB && lastA == lastB;
    }
    return a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0;
}

[[nodiscard]] std::uint32_t AllMask(const ReplicationRegistry& registry) noexcept {
    return registry.Count() == 32U ? UINT32_MAX : (1U << registry.Count()) - 1U;
}
[[nodiscard]] std::size_t MaskBytes(const ReplicationRegistry& registry) { return registry.Count() <= 16U ? 2U : 4U; }
void WriteMask(ByteWriter& writer, const ReplicationRegistry& registry, std::uint32_t mask) {
    if (MaskBytes(registry) == 2U) writer.U16(static_cast<std::uint16_t>(mask)); else writer.U32(mask);
}
[[nodiscard]] std::uint32_t ReadMask(ByteReader& reader, const ReplicationRegistry& registry) {
    return MaskBytes(registry) == 2U ? reader.U16() : reader.U32();
}
[[nodiscard]] std::size_t EntryHeaderBytes(const ReplicationRegistry& registry, std::uint32_t removed) {
    return 6U + MaskBytes(registry) * (removed == 0U ? 1U : 2U);
}

// True if update id `later` is newer than `earlier` in 32 bit wrap-around order.
[[nodiscard]] bool IsNewer(std::uint32_t later, std::uint32_t earlier) noexcept {
    const std::uint32_t difference = later - earlier;
    return difference != 0U && difference < 0x80000000U;
}

} // namespace

// ------------------------------------------------------------ registry

bool ReplicationRegistry::Add(std::unique_ptr<ComponentCodec> codec, std::string& error) {
    const auto audience = codec == nullptr ? ReplicationAudience::Everyone : codec->Audience();
    return Add(std::move(codec), audience, error);
}

bool ReplicationRegistry::Add(std::unique_ptr<ComponentCodec> codec, ReplicationAudience audience, std::string& error) {
    if (static_cast<unsigned>(audience) > static_cast<unsigned>(ReplicationAudience::InitialOnly)) { error = "invalid replication audience"; return false; }

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
    codec->audience_ = audience;
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
        mix(static_cast<std::uint8_t>(codec->Audience()));
        for (const char character : codec->Describe()) {
            mix(static_cast<std::uint8_t>(character));
        }
        mix(0U);
    }
    return hash;
}

// -------------------------------------------------------------- server

ReplicationServer::ReplicationServer(Host& host, kb::ecs::World& world, const ReplicationRegistry& components, const RpcRegistry& rpcs,
    ReplicationConfig config)
    : host_(&host), world_(&world), components_(&components), rpcs_(&rpcs), config_(config), slots_(std::make_unique<ReplicationSlots>(components)), grid_(std::make_unique<SpatialGrid>(*slots_, config.visibilityRadius)) {
    if (config_.tickRate == 0U || config_.minClientRate == 0U || config_.minClientRate > config_.maxClientRate) throw std::invalid_argument("invalid client rate configuration");
    changeSink_ = {this, [](kb::ecs::Entity entity, void* context) { static_cast<ReplicationServer*>(context)->MarkDirty(entity); }};
    std::vector<kb::ecs::ComponentId> watched;
    for (std::size_t index = 0; index < components.Count(); ++index) {
        audienceMasks_[static_cast<std::size_t>(components.At(index).Audience())] |= 1U << index;
        captureScratch_.resize(std::max(captureScratch_.size(), components.At(index).EncodedSize()));
        const auto id = components.At(index).SourceComponent(world);
        if (std::ranges::find(watched, id) != watched.end()) continue;
        watched.push_back(id);
        const auto observers = components.At(index).ObserveChanges(world, changeSink_);
        observers_.insert(observers_.end(), observers.begin(), observers.end());
    }
    if (std::ranges::find(watched, world.RegisterComponent<kb::scene::TransformComponent>()) == watched.end()) {
        const auto observers = ObserveReplicationChanges<kb::scene::TransformComponent>(world, changeSink_);
        observers_.insert(observers_.end(), observers.begin(), observers.end());
    }
    if (std::ranges::find(observers_, kb::ecs::ObserverId{0U}) != observers_.end()) {
        for (const auto observer : observers_) world.DestroyObserver(observer);
        throw std::runtime_error("replication change observers could not be registered");
    }
}

ReplicationServer::~ReplicationServer() {
    for (const auto observer : observers_) world_->DestroyObserver(observer);
}

void ReplicationServer::MarkDirty(kb::ecs::Entity entity) {
    const auto id = slots_->IdOf(entity);
    auto* slot = slots_->Find(id);
    if (slot == nullptr || slot->dirty) return;
    slot->dirty = true;
    dirty_.push_back(id);
}

std::size_t ReplicationServer::ReplicatedCount() const noexcept { return slots_->Size(); }
bool ReplicationServer::IsReplicated(kb::ecs::Entity entity) const noexcept { return slots_->IdOf(entity) != 0U; }
std::vector<NetworkId> ReplicationServer::NetworkIds() const {
    std::vector<NetworkId> ids;
    ids.reserve(slots_->Size());
    for (auto index : slots_->Active()) ids.push_back(MakeNetworkId(index, slots_->At(index)->generation));
    std::ranges::sort(ids);
    return ids;
}

ReplicateResult ReplicationServer::Replicate(kb::ecs::Entity entity, std::uint64_t owner) {
    if (!world_->IsAlive(entity)) return ReplicateResult::NotAlive;
    if (IsReplicated(entity)) return ReplicateResult::AlreadyReplicated;
    const auto id = slots_->Allocate(entity, owner);
    if (id == 0U) {
        host_->Diagnostics().Report(DiagCode::EntityLimit, "network slot format exhausted; alive=" + std::to_string(slots_->Size()));
        return ReplicateResult::Limit;
    }
    if (dirty_.capacity() < slots_->Size()) dirty_.reserve(std::max(slots_->Size(), dirty_.capacity() * 2U));
    ++visibilityRevision_;
    AddOwner(NetIndex(id));
    slots_->Find(id)->changedTick = lastTick_ + 1U;
    Capture(NetIndex(id), *slots_->Find(id));
    if (objectWarningArmed_ && slots_->Size() >= 1'000'000U) {
        objectWarningArmed_ = false;
        const auto megabytes = slots_->Size() * (sizeof(ReplicationSlot) + slots_->Stride()) / (1024U * 1024U);
        host_->Diagnostics().Report(DiagCode::ObjectCountWarning, std::to_string(slots_->Size()) + " network objects, about " +
            std::to_string(megabytes) + " MB of replication tables; set a visibility radius");
    }
    return ReplicateResult::Ok;
}

bool ReplicationServer::Unreplicate(NetworkId id) {
    if (slots_->Find(id) == nullptr) return false;
    static_cast<void>(SetAlwaysRelevant(id, false));
    grid_->Remove(NetIndex(id));
    RemoveOwner(NetIndex(id));
    slots_->Erase(id);
    ++visibilityRevision_;
    if (slots_->Size() < 950'000U) objectWarningArmed_ = true;
    return true;
}

bool ReplicationServer::SetOwner(NetworkId id, std::uint64_t owner) {
    auto* slot = slots_->Find(id);
    if (slot == nullptr) return false;
    const auto oldOwner = slot->owner;
    RemoveOwner(NetIndex(id));
    slot->owner = owner;
    AddOwner(NetIndex(id));
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgOwner);
    writer.U64(id);
    writer.U64(owner);
    for (auto& [client, state] : clients_) {
        if (auto* known = FindKnown(state, id); state.ready && known != nullptr) {
            known->epoch = state.nextEpoch++;
            const auto newlyAllowed = AudienceMask(client, owner, false) & ~AudienceMask(client, oldOwner, false);
            known->presentMask &= ~newlyAllowed;
            for (std::size_t component = 0; component < components_->Count(); ++component) if ((newlyAllowed & (1U << component)) != 0U) known->componentTicks[component] = 0U;
            static_cast<void>(SendReliable(client, message));
        }
    }
    MarkDirty(slot->entity);
    return true;
}

kb::ecs::Entity ReplicationServer::EntityOf(NetworkId id) const {
    const auto* slot = slots_->Find(id);
    return slot == nullptr ? kb::ecs::Entity{} : slot->entity;
}
NetworkId ReplicationServer::NetworkIdOf(kb::ecs::Entity entity) const { return slots_->IdOf(entity); }
std::uint64_t ReplicationServer::OwnerOf(NetworkId id) const {
    const auto* slot = slots_->Find(id);
    return slot == nullptr ? 0U : slot->owner;
}

std::size_t ReplicationServer::ReadyClients() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(clients_, [](const auto& entry) { return entry.second.ready; }));
}

std::size_t ReplicationServer::VisibleCount(std::uint64_t client) const {
    const auto found = clients_.find(client);
    return found == clients_.end() ? 0U : found->second.known.size();
}

std::vector<NetworkId> ReplicationServer::VisibleIds(std::uint64_t client) const {
    std::vector<NetworkId> ids;
    const auto found = clients_.find(client);
    if (found != clients_.end()) for (const auto& known : found->second.known) ids.push_back(known.id);
    return ids;
}

ReplicationServer::Known* ReplicationServer::FindKnown(ClientState& state, NetworkId id) const {
    const auto index = NetIndex(id);
    if (index >= state.knownIndexBySlot.size()) return nullptr;
    const auto position = state.knownIndexBySlot[index];
    if (position >= state.known.size() || state.known[position].id != id) return nullptr;
    return &state.known[position];
}

void ReplicationServer::AddKnown(ClientState& state, NetworkId id) {
    const auto index = NetIndex(id);
    if (state.knownIndexBySlot.size() <= index) state.knownIndexBySlot.resize(static_cast<std::size_t>(index) + 1U, UINT32_MAX);
    const auto found = std::ranges::lower_bound(state.known, index, {}, [](const Known& known) { return NetIndex(known.id); });
    const auto position = static_cast<std::size_t>(found - state.known.begin());
    Known known;
    known.id = id;
    known.epoch = state.nextEpoch++;
    state.known.insert(found, known);
    state.baselineArena.insert(state.baselineArena.begin() + static_cast<std::ptrdiff_t>(position * slots_->Stride()), slots_->Stride(), 0U);
    for (std::size_t i = position; i < state.known.size(); ++i) state.knownIndexBySlot[NetIndex(state.known[i].id)] = static_cast<std::uint32_t>(i);
}

void ReplicationServer::EraseKnown(ClientState& state, NetworkId id) {
    if (FindKnown(state, id) == nullptr) return;
    const auto position = state.knownIndexBySlot[NetIndex(id)];
    state.known.erase(state.known.begin() + position);
    const auto first = state.baselineArena.begin() + static_cast<std::ptrdiff_t>(position * slots_->Stride());
    state.baselineArena.erase(first, first + static_cast<std::ptrdiff_t>(slots_->Stride()));
    state.knownIndexBySlot[NetIndex(id)] = UINT32_MAX;
    for (std::size_t i = position; i < state.known.size(); ++i) state.knownIndexBySlot[NetIndex(state.known[i].id)] = static_cast<std::uint32_t>(i);
}

std::span<std::uint8_t> ReplicationServer::BaselineBytes(ClientState& state, NetworkId id, std::size_t component) const {
    const auto size = slots_->EncodedSize(component);
    if (size == 0U) return {};
    const auto offset = state.knownIndexBySlot[NetIndex(id)] * slots_->Stride() + slots_->Offset(component);
    return { state.baselineArena.data() + offset, size };
}

void ReplicationServer::AddOwner(std::uint32_t index) {
    auto& slot = *slots_->At(index);
    auto& list = owned_[slot.owner];
    slot.ownerPosition = static_cast<std::uint32_t>(list.size());
    list.push_back(index);
}
void ReplicationServer::RemoveOwner(std::uint32_t index) {
    auto& slot = *slots_->At(index);
    auto found = owned_.find(slot.owner);
    auto& list = found->second;
    list[slot.ownerPosition] = list.back();
    slots_->At(list.back())->ownerPosition = slot.ownerPosition;
    list.pop_back();
    if (list.empty()) owned_.erase(found);
}

bool ReplicationServer::SetAlwaysRelevant(NetworkId id, bool value) {
    auto* slot = slots_->Find(id);
    if (slot == nullptr) return false;
    if (slot->alwaysRelevant == value) return true;
    if (value) {
        slot->globalPosition = static_cast<std::uint32_t>(globals_.size());
        globals_.push_back(NetIndex(id));
    } else {
        globals_[slot->globalPosition] = globals_.back();
        slots_->At(globals_.back())->globalPosition = slot->globalPosition;
        globals_.pop_back();
    }
    slot->alwaysRelevant = value;
    return true;
}

bool ReplicationServer::Relevant(std::uint64_t client, const ClientState& state, std::uint32_t index, bool known) const {
    const auto* slot = slots_->At(index);
    if (slot == nullptr) return false;
    if (config_.visibilityRadius == 0.0F || slot->alwaysRelevant || slot->owner == client) return true;
    const auto* viewer = slots_->At(state.viewerSlot);
    if (viewer == nullptr || !viewer->hasPosition || !slot->hasPosition) return false;
    const auto dx = slot->x - viewer->x, dy = slot->y - viewer->y;
    const auto radius = config_.visibilityRadius * (known ? 1.15F : 1.0F);
    return dx * dx + dy * dy <= radius * radius;
}

std::uint32_t ReplicationServer::AudienceMask(std::uint64_t client, std::uint64_t owner, bool spawn) const {
    return audienceMasks_[static_cast<std::size_t>(ReplicationAudience::Everyone)] |
        audienceMasks_[static_cast<std::size_t>(client == owner ? ReplicationAudience::Owner : ReplicationAudience::Others)] |
        (spawn ? audienceMasks_[static_cast<std::size_t>(ReplicationAudience::InitialOnly)] : 0U);
}

void ReplicationServer::Capture(std::uint32_t slot, ReplicationSlot& replicated) {
    ++stats_.capturedEntities;
    const auto* transform = world_->TryGet<kb::scene::TransformComponent>(replicated.entity);
    if (transform != nullptr) {
        const auto& position = transform->worldPosition;
        grid_->Move(slot, position.x, config_.gridPlane == GridPlane::XZ ? position.z : position.y);
    } else {
        grid_->Remove(slot);
        replicated.hasPosition = false;
    }
    const auto previous = replicated.presentMask;
    replicated.presentMask = 0U;
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        const auto bytes = slots_->Bytes(slot, index);
        const auto captured = std::span(captureScratch_).first(bytes.size());
        const bool present = components_->At(index).Capture(*world_, replicated.entity, captured);
        const bool had = (previous & (1U << index)) != 0U;
        if (present != had || (present && std::memcmp(bytes.data(), captured.data(), bytes.size()) != 0)) {
            replicated.componentTicks[index] = replicated.changedTick;
            if (present) std::copy(captured.begin(), captured.end(), bytes.begin());
        }
        if (present) {
            replicated.presentMask |= 1U << index;
        }
    }
}

bool ReplicationServer::SendReliable(std::uint64_t client, std::span<const std::uint8_t> message) {
    if (host_->Send(client, kReliableChannel, message) == SendResult::Queued) {
        return true;
    }
    ++stats_.sendBusy;
    host_->Diagnostics().Report(DiagCode::SendQueueFull, "replication send queue refused a message");
    return false;
}

bool ReplicationServer::SendSpawn(std::uint64_t client, ClientState& state, NetworkId id, const ReplicationSlot& replicated, std::uint32_t tick) {
    const auto spawnMask = replicated.presentMask & AudienceMask(client, replicated.owner, true);
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgSpawn);
    writer.U64(id);
    writer.U64(replicated.owner);
    writer.U32(tick);
    WriteMask(writer, *components_, spawnMask);
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        if ((spawnMask & (1U << index)) != 0U) {
            writer.Bytes(slots_->Bytes(NetIndex(id), index));
        }
    }
    const auto charged = message.size() + Session::MessageOverhead(true);
    if (state.tokens < static_cast<double>(charged) || !SendReliable(client, message)) {
        return false;
    }
    state.tokens -= static_cast<double>(charged);
    AddKnown(state, id);
    auto& known = *FindKnown(state, id);
    known.presentMask = spawnMask;
    known.componentTicks.fill(tick);
    for (std::size_t component = 0; component < components_->Count(); ++component) {
        if ((spawnMask & (1U << component)) == 0U) continue;
        const auto bytes = slots_->Bytes(NetIndex(id), component);
        const auto baseline = BaselineBytes(state, id, component);
        std::copy(bytes.begin(), bytes.end(), baseline.begin());
    }
    ++stats_.spawnsSent;
    return true;
}

void ReplicationServer::Despawn(std::uint64_t client, ClientState& state, NetworkId id) {
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgDespawn);
    writer.U64(id);
    if (SendReliable(client, message)) {
        EraseKnown(state, id);
        ++stats_.despawnsSent;
    }
}

void ReplicationServer::UpdateVisibility(std::uint64_t client, ClientState& state, std::uint32_t tick) {
    ProfileScope profile(config_.profile, stats_.visibilityNs);
    if (config_.visibilityRadius == 0.0F && state.visibilityRevision == visibilityRevision_) return;
    bool complete = true;
    const auto owner = owned_.find(client);
    const auto* viewer = slots_->At(state.viewerSlot);
    if (viewer == nullptr || viewer->owner != client || !viewer->hasPosition) {
        state.viewerSlot = 0U;
        if (owner != owned_.end()) for (const auto index : owner->second) {
            if (slots_->At(index)->hasPosition) { state.viewerSlot = index; break; }
        }
    }
    const auto seen = VisibleIds(client);
    for (const auto id : seen) {
        ++stats_.candidatesExamined;
        if (slots_->Find(id) == nullptr || !Relevant(client, state, NetIndex(id), true)) {
            Despawn(client, state, id);
            if (FindKnown(state, id) != nullptr) complete = false;
        }
    }
    std::vector<std::uint32_t> candidates;
    if (config_.visibilityRadius == 0.0F) {
        candidates.assign(slots_->Active().begin(), slots_->Active().end());
    } else {
        viewer = slots_->At(state.viewerSlot);
        if (viewer != nullptr) grid_->Candidates(viewer->x, viewer->y, candidates);
        candidates.insert(candidates.end(), globals_.begin(), globals_.end());
        if (owner != owned_.end()) candidates.insert(candidates.end(), owner->second.begin(), owner->second.end());
    }
    std::ranges::sort(candidates);
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (const auto index : candidates) {
        ++stats_.candidatesExamined;
        const auto& slot = *slots_->At(index);
        const auto id = MakeNetworkId(index, slot.generation);
        if (FindKnown(state, id) != nullptr || !Relevant(client, state, index, false)) continue;
        if (!SendSpawn(client, state, id, slot, tick)) { complete = false; break; }
    }
    if (complete) state.visibilityRevision = visibilityRevision_;
}

void ReplicationServer::BuildUpdates(std::uint64_t client, ClientState& state, std::uint32_t tick) {
    if (state.known.empty() || state.breaker) return;
    struct Candidate { Known* known; std::uint32_t changed, removed; };
    std::vector<Candidate> order;
    order.reserve(state.known.size());
    const auto* viewer = slots_->At(state.viewerSlot);
    {
    ProfileScope profile(config_.profile, stats_.candidatesNs);
    for (auto& known : state.known) {
        const auto* slot = slots_->Find(known.id);
        if (slot == nullptr) continue;
        known.tier = 0U;
        if (config_.visibilityRadius > 0.0F && slot->owner != client && !slot->alwaysRelevant && viewer != nullptr && slot->hasPosition) {
            const float dx = slot->x - viewer->x, dy = slot->y - viewer->y;
            const float distanceSquared = dx * dx + dy * dy;
            const float radiusSquared = config_.visibilityRadius * config_.visibilityRadius;
            known.tier = distanceSquared < radiusSquared * 0.0625F ? 0U : distanceSquared < radiusSquared * 0.25F ? 1U : 2U;
        }
        const auto period = 1U << known.tier;
        const auto allowed = AudienceMask(client, slot->owner, false);
        std::uint32_t changed = 0U, removed = 0U;
        for (std::size_t index = 0U; index < components_->Count(); ++index) {
            if ((audienceMasks_[static_cast<std::size_t>(ReplicationAudience::InitialOnly)] & (1U << index)) != 0U) continue;
            const auto bit = 1U << index;
            const bool present = (slot->presentMask & allowed & bit) != 0U;
            const bool had = (known.presentMask & bit) != 0U;
            if (present && (!had || (slot->componentTicks[index] > known.componentTicks[index] &&
                !EncodedEqual(BaselineBytes(state, known.id, index), slots_->Bytes(NetIndex(known.id), index))))) changed |= bit;
            else if (!present && had) removed |= bit;
        }
        // An acknowledgement of another group must not hide an unacknowledged delta.
        if ((changed | removed) == 0U) continue;
        known.accum = std::min(1'000'000.0F, known.accum + static_cast<float>(4U >> known.tier));
        if (tick % period != NetIndex(known.id) % period) continue;
        order.push_back({&known, changed, removed});
    }
    }
    const auto priority = [](const Candidate& left, const Candidate& right) {
        const auto* a = left.known;
        const auto* b = right.known;
        if ((a->accum >= 8.0F) != (b->accum >= 8.0F)) return a->accum >= 8.0F;
        if (a->accum != b->accum) return a->accum > b->accum;
        return a->id < b->id;
    };
    std::size_t minimumEntry = SIZE_MAX;
    for (const auto& candidate : order) {
        auto size = EntryHeaderBytes(*components_, candidate.removed) + MaskedSize(*slots_, candidate.changed);
        if (size + kStateHeaderBytes > config_.maxUpdateBytes) {
            size = SIZE_MAX;
            auto pending = candidate.changed | candidate.removed;
            while (pending != 0U) {
                const auto bit = pending & (~pending + 1U);
                const auto part = EntryHeaderBytes(*components_, candidate.removed & bit) + MaskedSize(*slots_, candidate.changed & bit);
                size = std::min(size, part + kStateHeaderBytes > config_.maxUpdateBytes ? EntryHeaderBytes(*components_, 0U) : part);
                pending &= ~bit;
            }
        }
        minimumEntry = std::min(minimumEntry, size);
    }
    const auto selected = std::min(order.size(), static_cast<std::size_t>(state.tokens) / std::max<std::size_t>(1U, minimumEntry));
    if (selected == 0U) return;
    {
        ProfileScope profile(config_.profile, stats_.orderingNs);
        if (selected < order.size()) std::nth_element(order.begin(), order.begin() + selected, order.end(), priority);
        order.resize(selected);
        std::ranges::sort(order, priority);
    }
    ProfileScope profile(config_.profile, stats_.encodingNs);

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
        const auto charged = message.size() + Session::MessageOverhead(false);
        if (state.tokens < static_cast<double>(charged)) return false;
        if (host_->Send(client, kUnreliableChannel, message) != SendResult::Queued) {
            ++stats_.sendBusy;
            host_->Diagnostics().Report(DiagCode::SendQueueFull, "replication send queue refused a message");
            return false;
        }
        state.nextUpdateId = updateId + 1U == 0U ? 1U : updateId + 1U;
        state.tokens -= static_cast<double>(charged);
        for (const auto& entry : entries) {
            if (auto* known = FindKnown(state, entry.id)) { known->accum = 0.0F; known->sentTick = tick; }
        }
        if (state.inFlight.empty()) state.lastAckAt = nowMilliseconds_;
        state.inFlight[updateId] = SentUpdate{std::move(entries), std::move(body), nowMilliseconds_};
        while (state.inFlight.size() > config_.maxUpdatesInFlight) {
            if (!state.inFlight.begin()->second.lossCounted) ++state.lostInWindow;
            state.inFlight.erase(state.inFlight.begin());
        }
        ++stats_.stateMessagesSent;
        body.clear();
        entries.clear();
        count = 0U;
        return true;
    };

    bool finished = true;
    for (const auto& candidate : order) {
        const NetworkId id = candidate.known->id;
        const auto* replicatedFound = slots_->Find(id);
        if (replicatedFound == nullptr) {
            continue;
        }
        Known& baseline = *FindKnown(state, id);
        const auto changed = candidate.changed, removed = candidate.removed;
        const bool split = kStateHeaderBytes + EntryHeaderBytes(*components_, removed) + MaskedSize(*slots_, changed) > config_.maxUpdateBytes;
        std::uint32_t pending = (changed | removed);
        while (pending != 0U) {
            std::uint32_t part = pending;
            if (split) {
                part = (pending & (~pending + 1U));
            }
            pending = (pending & ~part);
            const std::uint32_t partChanged = (changed & part);
            const std::uint32_t partRemoved = (removed & part);
            if (kStateHeaderBytes + EntryHeaderBytes(*components_, partRemoved) + MaskedSize(*slots_, partChanged) > config_.maxUpdateBytes) {
                for (std::size_t index = 0U; index < components_->Count(); ++index) {
                    if ((partChanged & (1U << index)) == 0U) continue;
                    char detail[160]{};
                    std::snprintf(detail, sizeof(detail), "entity=%llu component=%s bytes=%zu limit=%zu", static_cast<unsigned long long>(id),
                        components_->At(index).Name().c_str(), components_->At(index).EncodedSize(), config_.maxUpdateBytes);
                    host_->Diagnostics().Report(DiagCode::OversizeRecord, detail);
                }
                continue;
            }
            const std::size_t entrySize = EntryHeaderBytes(*components_, partRemoved) + MaskedSize(*slots_, partChanged);
            if (kStateHeaderBytes + body.size() + entrySize > config_.maxUpdateBytes || count == kMaxEntriesPerState) {
                if (!flush()) {
                    finished = false;
                    break;
                }
            }
            if (static_cast<double>(kStateHeaderBytes + Session::MessageOverhead(false) + body.size() + entrySize) > state.tokens) {
                finished = false;
                break;
            }
            ByteWriter writer(body);
            writer.U32(NetIndex(id));
            writer.U8(static_cast<std::uint8_t>(NetGeneration(id)));
            writer.U8(partRemoved == 0U ? 0U : 1U);
            WriteMask(writer, *components_, partChanged);
            if (partRemoved != 0U) WriteMask(writer, *components_, partRemoved);
            SentEntry sent;
            sent.id = id;
            sent.epoch = baseline.epoch;
            sent.tick = tick;
            sent.changedMask = partChanged;
            sent.removedMask = partRemoved;
            sent.bytesOffset = body.size();
            for (std::size_t index = 0U; index < components_->Count(); ++index) {
                if ((partChanged & (1U << index)) != 0U) {
                    writer.Bytes(slots_->Bytes(NetIndex(id), index));
                }
            }
            entries.push_back(std::move(sent));
            ++count;
            ++stats_.entriesSent;
        }
        if (!finished) break;
    }
    if (!flush()) {
        finished = false;
    }
}

float ReplicationServer::RateScale(std::uint64_t client) const noexcept {
    const auto found = clients_.find(client);
    return found == clients_.end() ? 0.0F : found->second.rateScale;
}

std::uint32_t ReplicationServer::ClientRate(std::uint64_t client) const noexcept {
    const auto found = clients_.find(client);
    return found == clients_.end() ? 0U : found->second.bytesPerSecond;
}

void ReplicationServer::UpdateRate(std::uint64_t client, ClientState& state, std::uint64_t now) {
    for (auto& [id, update] : state.inFlight) {
        static_cast<void>(id);
        if (!update.lossCounted && now >= update.sentAt + 1000U) { update.lossCounted = true; ++state.lostInWindow; }
    }
    const auto* stats = host_->ClientStats(client);
    const auto rtt = stats == nullptr ? 0U : stats->smoothedRttMilliseconds;
    const auto second = now / 1000U;
    auto& sample = state.rttMinima[second % state.rttMinima.size()];
    if (sample.first != second || sample.second == 0U) sample = {second, rtt};
    else if (rtt != 0U) sample.second = std::min(sample.second, rtt);
    std::uint32_t minimum = UINT32_MAX;
    for (const auto& value : state.rttMinima) if (value.second != 0U && value.first + 10U > second) minimum = std::min(minimum, value.second);
    if (now >= state.windowAt + 1000U) {
        const auto total = state.ackedInWindow + state.lostInWindow;
        const bool bad = (total != 0U && state.lostInWindow * 10U > total) || (minimum != UINT32_MAX && rtt > 3ULL * minimum);
        if (bad) {
            if (!state.congested) state.congestionAt = state.windowAt;
            state.congested = true;
            state.quietAt = now;
            if (now >= state.congestionAt + 2000U) { state.rateScale = std::max(0.25F, state.rateScale * 0.5F); state.congestionAt = now; }
        } else {
            state.congested = false;
            if (now >= state.quietAt + 5000U && now >= state.recoveryAt + 1000U) {
                state.rateScale = std::min(1.0F, state.rateScale + 0.1F);
                state.recoveryAt = now;
            }
        }
        state.ackedInWindow = state.lostInWindow = 0U;
        state.windowAt = now;
    }
    const auto elapsed = now >= state.refillAt ? now - state.refillAt : 0U;
    const double rate = state.bytesPerSecond * static_cast<double>(state.rateScale);
    state.tokens = std::min(2.0 * rate / config_.tickRate, state.tokens + rate * static_cast<double>(elapsed) / 1000.0);
    state.refillAt = now;
    if (now > state.lastAckAt + 5000U && !state.inFlight.empty()) {
        if (!state.breaker) host_->Diagnostics().Report(DiagCode::RateLimited, "state circuit breaker: no acknowledgement from player=" + std::to_string(client));
        state.breaker = true;
    }
}

void ReplicationServer::Tick(std::uint32_t tick, std::uint64_t nowMilliseconds) {
    lastTick_ = tick;
    nowMilliseconds_ = nowMilliseconds;
    for (const auto id : dirty_) {
        auto* slot = slots_->Find(id);
        if (slot == nullptr) continue;
        slot->dirty = false;
        if (!world_->IsAlive(slot->entity)) {
            static_cast<void>(Unreplicate(id));
            continue;
        }
        slot->changedTick = tick;
        Capture(NetIndex(id), *slot);
    }
    dirty_.clear();
    if (slots_->Size() < 950'000U) objectWarningArmed_ = true;
    const bool unbounded = config_.visibilityRadius == 0.0F && (clients_.size() >= 64U || slots_->Size() >= 5000U);
    if (unbounded && unboundedWarningArmed_) {
        host_->Diagnostics().Report(DiagCode::VisibilityUnbounded, "all network objects are visible; set a visibility radius");
        unboundedWarningArmed_ = false;
    } else if (!unbounded) unboundedWarningArmed_ = true;
    for (auto& [client, state] : clients_) {
        if (!state.ready) {
            continue;
        }
        UpdateRate(client, state, nowMilliseconds);
        if (state.breaker) continue;
        UpdateVisibility(client, state, tick);
        BuildUpdates(client, state, tick);
    }
}

void ReplicationServer::HandleEvent(const NetEvent& event, std::uint64_t nowMilliseconds) {
    nowMilliseconds_ = nowMilliseconds;
    switch (event.type) {
    case NetEventType::Connected: {
        clients_[event.peer] = ClientState{};
        std::vector<std::uint8_t> hello;
        ByteWriter writer(hello);
        writer.U8(kMsgHello);
        writer.U64(components_->Fingerprint());
        writer.U64(rpcs_->Fingerprint());
        writer.U32(lastTick_);
        writer.U16(static_cast<std::uint16_t>(config_.tickRate));
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
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedMalformed");
        return;
    }
    switch (data[0]) {
    case kMsgHelloAck: {
        ByteReader reader(data.subspan(1));
        const std::uint64_t replication = reader.U64();
        const std::uint64_t rpc = reader.U64();
        const auto requested = reader.U32();
        if (channel != kReliableChannel || !reader.Ok() || reader.Remaining() != 0U) {
            ++stats_.rejectedMalformed;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedMalformed");
        } else if (replication != components_->Fingerprint() || rpc != rpcs_->Fingerprint()) {
            ++stats_.helloMismatch;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "helloMismatch");
            host_->Disconnect(client);
        } else {
            if (!state.ready) {
                state.bytesPerSecond = std::clamp(requested, config_.minClientRate, config_.maxClientRate);
                state.tokens = 2.0 * state.bytesPerSecond / config_.tickRate;
                state.refillAt = state.lastAckAt = state.windowAt = state.quietAt = state.recoveryAt = nowMilliseconds;
            }
            state.ready = true;
        }
        break;
    }
    case kMsgAck:
        if (channel != kUnreliableChannel) {
            ++stats_.rejectedMalformed;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedMalformed");
        } else if (!state.ready) {
            ++stats_.rejectedNotReady;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedNotReady");
        } else {
            HandleAck(state, data);
        }
        break;
    case kMsgRpc:
    case kMsgRpcUnreliable: {
        const bool reliable = data[0] == kMsgRpc;
        if (channel != (reliable ? kReliableChannel : kUnreliableChannel)) {
            ++stats_.rpcRejectedMalformed;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedMalformed");
        } else if (!state.ready) {
            ++stats_.rejectedNotReady;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedNotReady");
        } else {
            HandleRpc(client, channel, reliable, data, nowMilliseconds);
        }
        break;
    }
    default:
        ++stats_.rejectedMalformed;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedMalformed");
        break;
    }
}

void ReplicationServer::HandleAck(ClientState& state, std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const std::uint32_t latest = reader.U32();
    const std::uint32_t bits = reader.U32();
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rejectedMalformed");
        return;
    }
    if ((latest == 0U && bits != 0U) || IsNewer(latest, state.nextUpdateId - 1U)) { ++stats_.rejectedMalformed; return; }
    state.lastAckAt = nowMilliseconds_;
    state.breaker = false;
    ++stats_.acksReceived;
    const auto acknowledge = [&](std::uint32_t updateId) {
        const auto sent = state.inFlight.find(updateId);
        if (sent != state.inFlight.end()) {
            ApplyAcked(state, sent->second);
            ++state.ackedInWindow;
            state.lastAckAt = nowMilliseconds_;
            state.breaker = false;
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

void ReplicationServer::ApplyAcked(ClientState& state, const SentUpdate& update) {
    for (const SentEntry& entry : update.entries) {
        auto* baseline = FindKnown(state, entry.id);
        if (baseline == nullptr || entry.epoch != baseline->epoch) {
            continue; // despawned (and maybe spawned again) meanwhile, or a newer update was acknowledged first
        }
        Known& target = *baseline;
        auto offset = entry.bytesOffset;
        for (std::size_t index = 0U; index < components_->Count(); ++index) {
            const std::uint32_t bit = (1U << index);
            const auto size = (entry.changedMask & bit) != 0U ? components_->At(index).EncodedSize() : 0U;
            const auto encoded = std::span(update.bytes).subspan(offset, size);
            offset += size;
            if (entry.tick <= target.componentTicks[index]) continue;
            if (((entry.changedMask | entry.removedMask) & bit) != 0U) target.componentTicks[index] = entry.tick;
            if ((entry.changedMask & bit) != 0U) {
                const auto bytes = BaselineBytes(state, entry.id, index);
                std::copy(encoded.begin(), encoded.end(), bytes.begin());
                target.presentMask = (target.presentMask | bit);
            }
            if ((entry.removedMask & bit) != 0U) {
                target.presentMask = (target.presentMask & ~bit);
            }
        }
        target.baselineTick = entry.tick;
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
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedDirection");
        return;
    case RpcStatus::BadArguments:
        ++stats_.rpcRejectedArguments;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedArguments");
        return;
    default:
        ++stats_.rpcRejectedMalformed;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedMalformed");
        return;
    }
    const RpcDefinition* definition = rpcs_->Find(id);
    if (definition == nullptr || definition->reliable != reliable) {
        ++stats_.rpcRejectedMalformed;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedMalformed");
        return;
    }
    static_cast<void>(Dispatch(client, id, *definition, args, nowMilliseconds));
}

RpcStatus ReplicationServer::Dispatch(std::uint64_t caller, std::uint16_t id, const RpcDefinition& definition, std::span<const RpcValue> args, std::uint64_t now) {
    if (!limiter_.Allow(caller, id, definition.maxCallsPerSecond, now)) {
        ++stats_.rpcRejectedRate;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedRate");
        return RpcStatus::RateLimited;
    }
    if (definition.ownerArg >= 0) {
        const NetworkId target = std::get<NetworkId>(args[static_cast<std::size_t>(definition.ownerArg)]);
        const auto* replicated = slots_->Find(target);
        if (replicated == nullptr || replicated->owner != caller) {
            ++stats_.rpcRejectedPermission;
            host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedPermission");
            return RpcStatus::PermissionDenied;
        }
    }
    const auto handler = handlers_.find(id);
    if (handler == handlers_.end()) {
        ++stats_.rpcNoHandler;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcNoHandler");
        return RpcStatus::NoHandler;
    }
    try {
        handler->second(caller, args);
        ++stats_.rpcDispatched;
    } catch (const std::exception&) {
        ++stats_.rpcHandlerErrors;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcHandlerErrors");
    } catch (...) {
        ++stats_.rpcHandlerErrors;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcHandlerErrors");
    }
    return RpcStatus::Ok;
}

RpcStatus ReplicationServer::CallLocalRpc(std::uint64_t caller, const std::string& name, std::span<const RpcValue> args, std::uint64_t nowMilliseconds) {
    std::uint16_t id = 0U;
    const RpcDefinition* definition = rpcs_->FindByName(name, id);
    if (definition == nullptr) {
        return RpcStatus::UnknownRpc;
    }
    if (definition->direction != RpcDirection::ClientToServer) {
        return RpcStatus::WrongDirection;
    }
    if (const RpcStatus status = ValidateRpcArguments(*definition, args); status != RpcStatus::Ok) {
        ++stats_.rpcRejectedArguments;
        host_->Diagnostics().Report(DiagCode::RpcRejected, "rpcRejectedArguments");
        return status;
    }
    return Dispatch(caller, id, *definition, args, nowMilliseconds);
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
    : client_(&client), world_(&world), components_(&components), rpcs_(&rpcs), config_(config), slots_(std::make_unique<ReplicaSlots>(components)) {}

ReplicationClient::~ReplicationClient() = default;
std::size_t ReplicationClient::ReplicaCount() const noexcept { return slots_->Size(); }
void ReplicationClient::Reset() {
    for (const auto index : slots_->Active()) {
        const auto& slot = *slots_->At(index);
        const auto id = MakeNetworkId(index, slot.generation);
        if (onDespawn_) onDespawn_(id, slot.entity);
        if (world_->IsAlive(slot.entity)) world_->DestroyEntity(slot.entity);
    }
    slots_->Clear();
    ready_ = false;
    latestUpdate_ = 0U;
    receivedBits_ = 0U;
    ackDirty_ = false;
    lastAckSentAt_ = 0U;
    helloAckPending_ = false;
}
NetworkId ReplicationClient::NetworkIdOf(kb::ecs::Entity entity) const { return slots_->IdOf(entity); }
kb::ecs::Entity ReplicationClient::Find(NetworkId id) const {
    const auto* slot = slots_->Find(id);
    return slot == nullptr ? kb::ecs::Entity{} : slot->entity;
}
std::uint64_t ReplicationClient::OwnerOf(NetworkId id) const {
    const auto* slot = slots_->Find(id);
    return slot == nullptr ? 0U : slot->owner;
}
std::vector<NetworkId> ReplicationClient::ReplicaIds() const {
    std::vector<NetworkId> ids;
    ids.reserve(slots_->Size());
    for (const auto index : slots_->Active()) ids.push_back(MakeNetworkId(index, slots_->At(index)->generation));
    std::ranges::sort(ids);
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
    const std::uint16_t tickRate = reader.U16();
    if (!reader.Ok() || reader.Remaining() != 0U || tickRate == 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    if (replication != components_->Fingerprint() || rpc != rpcs_->Fingerprint()) {
        ++stats_.helloMismatch;
        diagnostics_.Report(DiagCode::RpcRejected, "helloMismatch");
        client_->Disconnect();
        return;
    }
    ready_ = true;
    serverTickRate_ = tickRate;
    helloAckPending_ = true; // sent by Flush, and repeated there until the session takes it
    Flush();
}

void ReplicationClient::HandleSpawn(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const NetworkId id = reader.U64();
    const std::uint64_t owner = reader.U64();
    const std::uint32_t tick = reader.U32();
    const std::uint32_t mask = ReadMask(reader, *components_);
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
    if (slots_->Find(id) != nullptr) return;
    if (NetIndex(id) == 0U || NetIndex(id) > kMaxNetSlots || slots_->At(NetIndex(id)) != nullptr) {
        ++stats_.rejectedMalformed;
        return;
    }
    const kb::ecs::Entity entity = world_->CreateEntity();
    for (std::size_t index = 0U; index < components_->Count(); ++index) {
        if ((mask & (1U << index)) != 0U) {
            components_->At(index).Apply(*world_, entity, parts[index]);
        }
    }
    static_cast<void>(slots_->Insert(id, entity, owner));
    auto& replica = *slots_->Find(id);
    replica.lastTick = tick;
    replica.componentTicks.fill(tick);
    ++stats_.spawnsApplied;
    if (onSpawn_) {
        onSpawn_(id, entity, owner);
    }
}

void ReplicationClient::HandleDespawn(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const NetworkId id = reader.U64();
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    auto* found = slots_->Find(id);
    if (found == nullptr) {
        return;
    }
    if (onDespawn_) {
        onDespawn_(id, found->entity);
    }
    if (world_->IsAlive(found->entity)) {
        world_->DestroyEntity(found->entity);
    }
    slots_->Erase(id);
    ++stats_.despawnsApplied;
}

void ReplicationClient::HandleOwner(std::span<const std::uint8_t> data) {
    ByteReader reader(data.subspan(1));
    const NetworkId id = reader.U64();
    const std::uint64_t owner = reader.U64();
    if (!reader.Ok() || reader.Remaining() != 0U) {
        ++stats_.rejectedMalformed;
        return;
    }
    auto* found = slots_->Find(id);
    if (found != nullptr) {
        found->owner = owner;
        ++stats_.ownerChanges;
    }
}

void ReplicationClient::HandleState(std::span<const std::uint8_t> data) {
    struct Entry {
        std::uint32_t index = 0U;
        std::uint8_t generation = 0U;
        std::uint32_t changed = 0U;
        std::uint32_t removed = 0U;
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
    const std::uint32_t valid = AllMask(*components_);
    std::vector<Entry> entries;
    entries.reserve(count);
    for (std::uint16_t index = 0U; index < count; ++index) {
        Entry entry;
        entry.index = reader.U32();
        entry.generation = reader.U8();
        const auto flags = reader.U8();
        if ((flags & ~1U) != 0U) { ++stats_.rejectedMalformed; return; }
        entry.changed = ReadMask(reader, *components_);
        entry.removed = (flags & 1U) != 0U ? ReadMask(reader, *components_) : 0U;
        if (!reader.Ok() || entry.index == 0U || ((entry.changed | entry.removed) & ~valid) != 0U || (entry.changed & entry.removed) != 0U ||
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
    bool missingReplica = false;
    bool appliedAny = false;
    for (const Entry& entry : entries) {
        auto* found = slots_->At(entry.index);
        if (found == nullptr) {
            ++stats_.entriesUnknownEntity;
            acknowledge = false;
            missingReplica = true;
            continue;
        }
        if (static_cast<std::uint8_t>(found->generation) != entry.generation) {
            ++stats_.entriesStale;
            acknowledge = false;
            continue;
        }
        bool applied = false;
        for (std::size_t component = 0U; component < components_->Count(); ++component) {
            if (((entry.changed | entry.removed) & (1U << component)) == 0U) continue;
            if (tick <= found->componentTicks[component]) {
                ++stats_.entriesStale;
                acknowledge = acknowledge && tick == found->componentTicks[component];
                continue;
            }
            found->componentTicks[component] = tick;
            applied = true;
            if ((entry.changed & (1U << component)) != 0U) {
                components_->At(component).Apply(*world_, found->entity, entry.parts[component]);
            }
            if ((entry.removed & (1U << component)) != 0U) {
                components_->At(component).Remove(*world_, found->entity);
            }
        }
        if (!applied) continue;
        appliedAny = true;
        found->lastTick = std::max(found->lastTick, tick);
        ++stats_.entriesApplied;
        if (onState_) {
            onState_(MakeNetworkId(entry.index, found->generation), found->entity, tick);
        }
    }
    ++stats_.stateMessagesApplied;
    if (!acknowledge) {
        // Report receiver liveness without acknowledging any unapplied entries.
        if (missingReplica || appliedAny) ackDirty_ = true;
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

void ReplicationClient::Flush(std::uint64_t nowMilliseconds) {
    if (!ready_) {
        return;
    }
    if (helloAckPending_) {
        std::vector<std::uint8_t> hello;
        ByteWriter helloWriter(hello);
        helloWriter.U8(kMsgHelloAck);
        helloWriter.U64(components_->Fingerprint());
        helloWriter.U64(rpcs_->Fingerprint());
        helloWriter.U32(config_.requestedBytesPerSecond);
        if (client_->Send(kReliableChannel, hello) != SendResult::Queued) {
            return;
        }
        helloAckPending_ = false;
    }
    const bool heartbeat = latestUpdate_ != 0U && nowMilliseconds >= lastAckSentAt_ + 1000U;
    if (!ackDirty_ && !heartbeat) {
        return;
    }
    std::vector<std::uint8_t> message;
    ByteWriter writer(message);
    writer.U8(kMsgAck);
    writer.U32(latestUpdate_);
    writer.U32(receivedBits_);
    if (client_->Send(kUnreliableChannel, message) == SendResult::Queued) {
        ackDirty_ = false;
        lastAckSentAt_ = nowMilliseconds;
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
