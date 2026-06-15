#pragma once

#include <cstddef>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <xmmintrin.h>
#endif

namespace kb::ecs {

enum class QueryPrefetchAccess {
    Read,
    Write,
};

inline void PrefetchQueryMemory(const void* address, QueryPrefetchAccess access = QueryPrefetchAccess::Read) noexcept {
    if (address == nullptr) {
        return;
    }

#if defined(__GNUC__) || defined(__clang__)
    if (access == QueryPrefetchAccess::Write) {
        __builtin_prefetch(address, 1, 3);
    } else {
        __builtin_prefetch(address, 0, 3);
    }
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    (void)access;
    _mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
#else
    (void)address;
    (void)access;
#endif
}

template <typename... Pointers>
inline void PrefetchQueryComponentsAt(std::size_t index, QueryPrefetchAccess access, Pointers... pointers) noexcept {
    (PrefetchQueryMemory(pointers == nullptr ? nullptr : static_cast<const void*>(pointers + index), access), ...);
}

} // namespace kb::ecs
