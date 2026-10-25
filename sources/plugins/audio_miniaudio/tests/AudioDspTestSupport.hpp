#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace kb::audio_miniaudio::tests {

struct AllocationCounter {
    struct alignas(std::max_align_t) Header { std::size_t bytes; };
    std::size_t calls = 0U;
    std::size_t failAt = 0U;
    std::size_t live = 0U;
    std::size_t bytes = 0U;
    std::size_t peak = 0U;

    static void* Allocate(std::size_t size, void* user) {
        auto& state = *static_cast<AllocationCounter*>(user);
        if (++state.calls == state.failAt || size > std::numeric_limits<std::size_t>::max() - sizeof(Header)) return nullptr;
        auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
        if (header == nullptr) return nullptr;
        header->bytes = size;
        ++state.live;
        state.bytes += size;
        state.peak = std::max(state.peak, state.bytes);
        return header + 1;
    }
    static void Free(void* memory, void* user) {
        if (memory == nullptr) return;
        auto& state = *static_cast<AllocationCounter*>(user);
        auto* header = static_cast<Header*>(memory) - 1;
        if (state.live == 0U || state.bytes < header->bytes) throw std::runtime_error{"DSP allocation ownership is inconsistent"};
        --state.live;
        state.bytes -= header->bytes;
        std::free(header);
    }
    static void* Reallocate(void* memory, std::size_t size, void* user) {
        if (memory == nullptr) return Allocate(size, user);
        if (size == 0U) { Free(memory, user); return nullptr; }
        const auto oldSize = (static_cast<Header*>(memory) - 1)->bytes;
        void* replacement = Allocate(size, user);
        if (replacement == nullptr) return nullptr;
        std::copy_n(static_cast<const unsigned char*>(memory), std::min(oldSize, size), static_cast<unsigned char*>(replacement));
        Free(memory, user);
        return replacement;
    }
};

} // namespace kb::audio_miniaudio::tests
