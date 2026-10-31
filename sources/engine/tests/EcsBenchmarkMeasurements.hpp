#pragma once

#include <flecs.h>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <cstdint>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <psapi.h>
#include <malloc.h>
#endif

// One executable owns these replacement operators. Counts describe allocation
// requests, not live blocks or allocated bytes; Flecs uses its own C allocator.
namespace ecs_benchmark {
inline std::atomic<std::size_t> cppAllocations{0};
struct Sample {
    std::size_t cpp = cppAllocations.load();
    std::int64_t flecs = ecs_os_api_malloc_count + ecs_os_api_calloc_count + ecs_os_api_realloc_count;
};
inline void PrintAllocations(const char* phase, Sample before) {
    const Sample after;
    std::cout << phase << "_allocations,cpp_new_calls=" << after.cpp - before.cpp
              << ",flecs_c_allocator_calls=" << after.flecs - before.flecs << ",basis=measured_requests\n";
}
inline void PrintMemory(const char* phase) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        throw std::runtime_error("Process memory query failed");
    std::cout << phase << "_memory,private_bytes=" << counters.PrivateUsage
              << ",working_set_bytes=" << counters.WorkingSetSize << ",basis=measured_process_total\n";
#else
    std::cout << phase << "_memory,basis=unavailable_on_this_platform\n";
#endif
}
}

void* operator new(std::size_t bytes) {
    ++ecs_benchmark::cppAllocations;
    if (auto* p = std::malloc(bytes ? bytes : 1U)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    ++ecs_benchmark::cppAllocations;
    const auto align = static_cast<std::size_t>(alignment);
#ifdef _WIN32
    auto* p = _aligned_malloc(bytes ? bytes : 1U, align);
#else
    auto* p = std::aligned_alloc(align, ((bytes ? bytes : 1U) + align - 1U) / align * align);
#endif
    if (p) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete(void* p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
void operator delete[](void* p, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(p, alignment); }
