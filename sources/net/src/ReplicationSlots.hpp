#pragma once

#include "kb/net/Replication.hpp"

#include <limits>
#include <unordered_map>

namespace kb::net {

inline constexpr std::uint32_t kMaxNetSlots = 0xFFFFFFFEU;

struct ReplicationSlot {
    kb::ecs::Entity entity;
    std::uint64_t owner = 0U;
    std::uint32_t generation = 0U;
    std::uint32_t changedTick = 0U;
    std::uint32_t presentMask = 0U;
    std::uint32_t activePosition = 0U;
    bool alive = false;
    std::uint64_t cellKey = 0U;
    std::uint32_t cellPosition = 0U;
    std::uint32_t ownerPosition = 0U;
    std::uint32_t globalPosition = 0U;
    float x = 0.0F, y = 0.0F;
    bool inGrid = false;
    bool hasPosition = false;
    bool alwaysRelevant = false;
    bool dirty = false;
};

struct ReplicaSlot {
    kb::ecs::Entity entity;
    std::uint64_t owner = 0U;
    std::uint32_t generation = 0U;
    std::uint32_t lastTick = 0U;
    std::array<std::uint32_t, kMaxReplicatedComponents> componentTicks{};
    std::uint32_t activePosition = 0U;
    bool alive = false;
};

template <typename Slot>
class ReplicationPages {
public:
    static constexpr std::uint32_t kPageSize = 4096U;

    explicit ReplicationPages(const ReplicationRegistry& registry) {
        for (std::size_t component = 0; component < registry.Count(); ++component) {
            offsets_[component] = stride_;
            sizes_[component] = registry.At(component).EncodedSize();
            stride_ += sizes_[component];
        }
        slotByEntity_.reserve(kPageSize);
    }

    [[nodiscard]] NetworkId Allocate(kb::ecs::Entity entity, std::uint64_t owner) {
        std::uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            if (nextIndex_ > kMaxNetSlots) return 0U;
            index = nextIndex_++;
        }
        auto& slot = Ensure(index);
        const auto generation = slot.generation;
        slot = Slot{};
        slot.generation = generation;
        Activate(index, slot, entity, owner);
        return MakeNetworkId(index, slot.generation);
    }

    bool Insert(NetworkId id, kb::ecs::Entity entity, std::uint64_t owner) {
        const auto index = NetIndex(id);
        if (id == 0U || index == 0U || index > kMaxNetSlots) return false;
        auto& slot = Ensure(index);
        if (slot.alive) return false;
        slot = Slot{};
        slot.generation = NetGeneration(id);
        Activate(index, slot, entity, owner);
        return true;
    }

    void Erase(NetworkId id) {
        auto* slot = Find(id);
        if (slot == nullptr) return;
        slotByEntity_.erase(slot->entity.Id());
        const auto position = slot->activePosition;
        const auto last = active_.back();
        active_[position] = last;
        At(last)->activePosition = position;
        active_.pop_back();
        slot->alive = false;
        ++slot->generation;
        free_.push_back(NetIndex(id));
    }

    [[nodiscard]] Slot* At(std::uint32_t index) const noexcept {
        const auto page = index / kPageSize;
        if (page >= pages_.size() || pages_[page] == nullptr) return nullptr;
        auto* slot = &pages_[page]->slots[index % kPageSize];
        return slot->alive ? slot : nullptr;
    }

    [[nodiscard]] Slot* Find(NetworkId id) const noexcept {
        auto* slot = At(NetIndex(id));
        return slot != nullptr && slot->generation == NetGeneration(id) ? slot : nullptr;
    }

    [[nodiscard]] NetworkId IdOf(kb::ecs::Entity entity) const noexcept {
        const auto found = slotByEntity_.find(entity.Id());
        if (found == slotByEntity_.end()) return 0U;
        return MakeNetworkId(found->second, At(found->second)->generation);
    }

    [[nodiscard]] std::span<std::uint8_t> Bytes(std::uint32_t index, std::size_t component) const {
        auto& page = *pages_[index / kPageSize];
        if (sizes_[component] == 0U) return {};
        return { page.blobs.get() + (index % kPageSize) * stride_ + offsets_[component], sizes_[component] };
    }

    [[nodiscard]] std::span<const std::uint32_t> Active() const noexcept { return active_; }
    [[nodiscard]] std::size_t Size() const noexcept { return active_.size(); }
    [[nodiscard]] std::size_t Stride() const noexcept { return stride_; }
    [[nodiscard]] std::size_t Offset(std::size_t component) const noexcept { return offsets_[component]; }

    void Clear() {
        pages_.clear();
        active_.clear();
        free_.clear();
        slotByEntity_.clear();
        nextIndex_ = 1U;
    }

private:
    struct Page {
        std::array<Slot, kPageSize> slots{};
        std::unique_ptr<std::uint8_t[]> blobs;
    };

    Slot& Ensure(std::uint32_t index) {
        const auto page = index / kPageSize;
        if (pages_.size() <= page) pages_.resize(static_cast<std::size_t>(page) + 1U);
        if (pages_[page] == nullptr) {
            pages_[page] = std::make_unique<Page>();
            if (stride_ != 0U) pages_[page]->blobs = std::make_unique<std::uint8_t[]>(kPageSize * stride_);
        }
        return pages_[page]->slots[index % kPageSize];
    }

    void Activate(std::uint32_t index, Slot& slot, kb::ecs::Entity entity, std::uint64_t owner) {
        slot.entity = entity;
        slot.owner = owner;
        slot.alive = true;
        slot.activePosition = static_cast<std::uint32_t>(active_.size());
        active_.push_back(index);
        slotByEntity_.emplace(entity.Id(), index);
    }

    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<std::uint32_t> active_;
    std::vector<std::uint32_t> free_;
    std::unordered_map<kb::ecs::Entity::IdType, std::uint32_t> slotByEntity_;
    std::array<std::size_t, kMaxReplicatedComponents> offsets_{};
    std::array<std::size_t, kMaxReplicatedComponents> sizes_{};
    std::size_t stride_ = 0U;
    std::uint32_t nextIndex_ = 1U;
};

class ReplicationSlots final : public ReplicationPages<ReplicationSlot> {
public:
    using ReplicationPages::ReplicationPages;
};
class ReplicaSlots final : public ReplicationPages<ReplicaSlot> {
public:
    using ReplicationPages::ReplicationPages;
};

} // namespace kb::net
