#pragma once
#include <miniaudio.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace kb::audio_miniaudio {
class MiniaudioAllocationTracker {
    struct alignas(std::max_align_t) Header { std::size_t size; };
    static void* Allocate(std::size_t size, void* user) noexcept {
        return Reallocate(nullptr, size, user);
    }
    static void Free(void* pointer, void* user) noexcept {
        if (pointer == nullptr) return;
        auto* header = static_cast<Header*>(pointer) - 1;
        static_cast<MiniaudioAllocationTracker*>(user)->current.fetch_sub(header->size, std::memory_order_relaxed);
        std::free(header);
    }
    static void* Reallocate(void* pointer, std::size_t size, void* user) noexcept {
        if (size == 0U) { Free(pointer, user); return nullptr; }
        if (size > std::numeric_limits<std::size_t>::max() - sizeof(Header)) return nullptr;
        auto& self = *static_cast<MiniaudioAllocationTracker*>(user);
        auto* old = pointer == nullptr ? nullptr : static_cast<Header*>(pointer) - 1;
        const auto oldSize = old == nullptr ? 0U : old->size;
        auto* header = static_cast<Header*>(std::realloc(old, sizeof(Header) + size));
        if (header == nullptr) return nullptr;
        header->size = size;
        std::uint64_t total = 0U;
        if (size >= oldSize) total = self.current.fetch_add(size - oldSize, std::memory_order_relaxed) + size - oldSize;
        else total = self.current.fetch_sub(oldSize - size, std::memory_order_relaxed) - (oldSize - size);
        auto peak = self.peak.load(std::memory_order_relaxed);
        while (peak < total && !self.peak.compare_exchange_weak(peak, total, std::memory_order_relaxed)) {}
        self.count.fetch_add(1U, std::memory_order_relaxed);
        return header + 1;
    }
public:
    std::atomic<std::uint64_t> current{0U}, peak{0U}, count{0U};
    [[nodiscard]] ma_allocation_callbacks Callbacks() noexcept { return {this, &Allocate, &Reallocate, &Free}; }
    void ResetPeak() noexcept { peak.store(current.load()); count.store(0U); }
};
} // namespace kb::audio_miniaudio
