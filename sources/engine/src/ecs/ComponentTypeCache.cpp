#include "ecs/component/ComponentTypeCache.hpp"

#include <atomic>
#include <cstdint>

namespace kb::ecs {

namespace {
std::atomic<std::uint32_t> gTypeInfoEpoch{ 1U };
}

void ComponentTypeCache::InvalidateTypeInfoEntries() noexcept {
    gTypeInfoEpoch.fetch_add(1U, std::memory_order_acq_rel);
}

ComponentId ComponentTypeCache::Find(std::type_index type) const noexcept {
    const auto it = componentIds_.find(type);
    return it == componentIds_.end() ? 0 : it->second;
}

ComponentId ComponentTypeCache::Find(const std::type_info& type) const noexcept {
    const std::uint32_t epoch = gTypeInfoEpoch.load(std::memory_order_acquire);
    const std::size_t start = (reinterpret_cast<std::uintptr_t>(&type) >> 4U) & (kFastSlots - 1U);
    for (std::size_t probe = 0U; probe < kFastProbes; ++probe) {
        const FastSlot& slot = fast_[(start + probe) & (kFastSlots - 1U)];
        const std::type_info* key = slot.key.load(std::memory_order_acquire);
        if (key == &type) {
            if (slot.epoch.load(std::memory_order_acquire) == epoch) {
                const ComponentId cached = slot.id.load(std::memory_order_acquire);
                if (cached != 0) return cached;
            }
            break;
        }
        if (key == nullptr) break;
    }
    const ComponentId found = Find(std::type_index{ type });
    if (found == 0) return 0;
    for (std::size_t probe = 0U; probe < kFastProbes; ++probe) {
        FastSlot& slot = fast_[(start + probe) & (kFastSlots - 1U)];
        const std::type_info* expected = nullptr;
        if (slot.key.compare_exchange_strong(expected, &type, std::memory_order_acq_rel) || expected == &type) {
            slot.id.store(found, std::memory_order_release);
            slot.epoch.store(epoch, std::memory_order_release);
            break;
        }
    }
    return found;
}

void ComponentTypeCache::Store(std::type_index type, ComponentId componentId) {
    if (componentId != 0) {
        componentIds_.emplace(type, componentId);
    }
}

void ComponentTypeCache::Clear() noexcept {
    componentIds_.clear();
    for (FastSlot& slot : fast_) {
        slot.key.store(nullptr, std::memory_order_relaxed);
        slot.id.store(0, std::memory_order_relaxed);
        slot.epoch.store(0, std::memory_order_relaxed);
    }
}

} // namespace kb::ecs
