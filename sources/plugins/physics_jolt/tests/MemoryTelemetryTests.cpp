#include "../JoltMemoryTelemetry.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
}
int main() {
    namespace memory = kb::physics_jolt::memory;
    JPH::RegisterDefaultAllocator();
    memory::Install();
    Check(*memory::Read() == 0U, "Initial allocation counter");
    for (std::size_t alignment : std::array<std::size_t, 5>{8, 16, 64, 256, 4096}) {
        void* pointer = JPH::AlignedAllocate(137U, alignment);
        Check(pointer && reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0U, "Aligned payload contract");
        Check(*memory::Read() == 137U, "Count payload without headers or padding");
        std::memset(pointer, 0xA5, 137U);
        JPH::AlignedFree(pointer);
        Check(*memory::Read() == 0U, "Aligned free releases exact count");
    }
    auto* pointer = static_cast<unsigned char*>(JPH::Allocate(31U));
    Check(pointer && reinterpret_cast<std::uintptr_t>(pointer) % 16U == 0U, "Default alignment");
    std::memset(pointer, 0x6B, 31U);
    pointer = static_cast<unsigned char*>(JPH::Reallocate(pointer, 31U, 257U));
    Check(pointer && *memory::Read() == 257U, "Reallocate growth");
    for (unsigned i = 0; i < 31U; ++i) Check(pointer[i] == 0x6B, "Reallocate preserves contents");
    Check(JPH::Reallocate(pointer, 257U, (std::numeric_limits<std::size_t>::max)()) == nullptr &&
        *memory::Read() == 257U && pointer[0] == 0x6B, "Failed reallocation preserves ownership and bytes");
    pointer = static_cast<unsigned char*>(JPH::Reallocate(pointer, 257U, 7U));
    Check(pointer && *memory::Read() == 7U && pointer[6] == 0x6B, "Reallocate shrink");
    Check(JPH::Reallocate(pointer, 7U, 0U) == nullptr && *memory::Read() == 0U, "Zero-size reallocation frees");
    JPH::Free(nullptr); JPH::AlignedFree(nullptr);
    Check(JPH::AlignedAllocate(10U, 3U) == nullptr && JPH::AlignedAllocate(10U, 0U) == nullptr,
        "Invalid alignment is rejected");
    const auto worker = [] {
        for (unsigned i = 0; i < 1000U; ++i) {
            void* allocation = JPH::AlignedAllocate(80U + i, 64U);
            Check(allocation != nullptr, "Concurrent allocation");
            JPH::AlignedFree(allocation);
        }
    };
    std::thread first{worker}, second{worker}; first.join(); second.join();
    Check(*memory::Read() == 0U, "Concurrent allocations balance after release");
    std::puts("Physics allocation accounting passed");
}
