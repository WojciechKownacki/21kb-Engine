#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kb::render::detail {

[[nodiscard]] constexpr std::uint32_t NextRenderResourceGeneration(std::uint32_t generation) noexcept {
    const std::uint32_t next = generation + 1U;
    return next == 0U ? 1U : next;
}

template <typename Resource>
class RenderResourceSlotPool final {
public:
    RenderResourceSlotPool() {
        slots_.push_back(Slot{});
    }

    template <typename Handle>
    [[nodiscard]] const Resource* Find(Handle handle) const noexcept {
        const std::uint32_t index = handle.Index();
        if (!IsLiveHandle(handle, index)) {
            return nullptr;
        }
        return &slots_[index].resource;
    }

    [[nodiscard]] std::uint32_t Allocate() {
        if (!freeSlots_.empty()) {
            const std::uint32_t index = freeSlots_.back();
            freeSlots_.pop_back();
            return index;
        }
        slots_.push_back(Slot{});
        return static_cast<std::uint32_t>(slots_.size() - 1U);
    }

    template <typename Value>
    void Activate(std::uint32_t index, Value&& resource) {
        Slot& slot = slots_[index];
        slot.resource = std::forward<Value>(resource);
        slot.occupied = true;
        slot.pendingDestroy = false;
    }

    template <typename Handle>
    [[nodiscard]] std::uint32_t MarkPendingDestroy(Handle handle) noexcept {
        const std::uint32_t index = handle.Index();
        if (!IsLiveHandle(handle, index)) {
            return 0U;
        }

        Slot& slot = slots_[index];
        slot.occupied = false;
        slot.pendingDestroy = true;
        return index;
    }

    template <typename Release>
    void Shutdown(Release&& release) noexcept {
        freeSlots_.clear();
        for (std::uint32_t index = 1U; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            release(slot.resource);
            slot.occupied = false;
            slot.pendingDestroy = false;
            slot.generation = NextRenderResourceGeneration(slot.generation);
            freeSlots_.push_back(index);
        }
    }

    template <typename Release>
    void ReleasePending(std::uint32_t index, Release&& release) noexcept {
        if (index >= slots_.size() || !slots_[index].pendingDestroy) {
            return;
        }

        Slot& slot = slots_[index];
        release(slot.resource);
        slot.generation = NextRenderResourceGeneration(slot.generation);
        slot.pendingDestroy = false;
        freeSlots_.push_back(index);
    }

    void Reserve(std::uint32_t slotCount) {
        if (slotCount == 0U) {
            return;
        }
        slots_.reserve(static_cast<std::size_t>(slotCount) + 1U);
        freeSlots_.reserve(slotCount);
    }

    [[nodiscard]] std::uint32_t Generation(std::uint32_t index) const noexcept {
        return slots_[index].generation;
    }

    [[nodiscard]] std::uint32_t LiveCount() const noexcept {
        std::uint32_t count = 0U;
        for (std::uint32_t index = 1U; index < slots_.size(); ++index) {
            count += slots_[index].occupied ? 1U : 0U;
        }
        return count;
    }

    [[nodiscard]] std::uint32_t SlotCapacity() const noexcept {
        return slots_.capacity() == 0U ? 0U : static_cast<std::uint32_t>(slots_.capacity() - 1U);
    }

    [[nodiscard]] std::uint32_t FreeSlotCount() const noexcept {
        return static_cast<std::uint32_t>(freeSlots_.size());
    }

private:
    template <typename Handle>
    [[nodiscard]] bool IsLiveHandle(Handle handle, std::uint32_t index) const noexcept {
        if (!handle.IsValid() || index == 0U || index >= slots_.size()) {
            return false;
        }

        const Slot& slot = slots_[index];
        return slot.occupied && !slot.pendingDestroy && slot.generation == handle.Generation();
    }

    struct Slot {
        Resource resource{};
        std::uint32_t generation = 1U;
        bool occupied = false;
        bool pendingDestroy = false;
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeSlots_;
};

} // namespace kb::render::detail
