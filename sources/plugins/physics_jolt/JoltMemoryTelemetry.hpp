#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace kb::physics_jolt::memory {
#ifndef JPH_DISABLE_CUSTOM_ALLOCATOR
// Requested allocator bytes across this backend, including shared tables and all worlds.
// The instrumentation header and platform allocator padding are excluded.
struct Header { void* allocation; std::size_t bytes; };
inline std::atomic<std::uint64_t> residentBytes{0U};
inline JPH::AllocateFunction allocateBase = nullptr;
inline JPH::FreeFunction freeBase = nullptr;

inline void* AllocateAligned(std::size_t bytes, std::size_t alignment) {
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U) return nullptr;
    alignment = (std::max)(alignment, alignof(Header));
    if (alignment > (std::numeric_limits<std::size_t>::max)() - sizeof(Header)) return nullptr;
    const auto overhead = sizeof(Header) + alignment - 1U;
    if (bytes > (std::numeric_limits<std::size_t>::max)() - overhead) return nullptr;
    void* allocation = allocateBase(bytes + overhead);
    if (!allocation) return nullptr;
    const auto begin = reinterpret_cast<std::uintptr_t>(allocation) + sizeof(Header);
    auto* payload = reinterpret_cast<void*>((begin + alignment - 1U) & ~(static_cast<std::uintptr_t>(alignment) - 1U));
    auto* header = reinterpret_cast<Header*>(payload) - 1;
    *header = {allocation, bytes};
    residentBytes.fetch_add(bytes, std::memory_order_relaxed);
    return payload;
}
inline void* Allocate(std::size_t bytes) { return AllocateAligned(bytes, 16U); }
inline void Free(void* payload) {
    if (!payload) return;
    const auto header = *(reinterpret_cast<Header*>(payload) - 1);
    freeBase(header.allocation);
    residentBytes.fetch_sub(header.bytes, std::memory_order_relaxed);
}
inline void* Reallocate(void* payload, std::size_t, std::size_t bytes) {
    if (!payload) return Allocate(bytes);
    if (bytes == 0U) { Free(payload); return nullptr; }
    void* replacement = Allocate(bytes);
    if (!replacement) return nullptr;
    const auto previousBytes = (reinterpret_cast<Header*>(payload) - 1)->bytes;
    std::memcpy(replacement, payload, (std::min)(previousBytes, bytes));
    Free(payload);
    return replacement;
}
// Called once under the backend initialization lock, before any physics allocations.
inline void Install() {
    allocateBase = JPH::Allocate;
    freeBase = JPH::Free;
    JPH::Allocate = &Allocate;
    JPH::Reallocate = &Reallocate;
    JPH::Free = &Free;
    JPH::AlignedAllocate = &AllocateAligned;
    JPH::AlignedFree = &Free;
}
inline std::optional<std::uint64_t> Read() { return residentBytes.load(std::memory_order_relaxed); }
#else
inline void Install() {}
inline std::optional<std::uint64_t> Read() { return std::nullopt; }
#endif
} // namespace kb::physics_jolt::memory
